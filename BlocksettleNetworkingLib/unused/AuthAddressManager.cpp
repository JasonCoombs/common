/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "AuthAddressManager.h"
#include <spdlog/spdlog.h>
#include <memory>
#include <QtConcurrent/QtConcurrentRun>
#include "AddressVerificator.h"
#include "ApplicationSettings.h"
#include "ArmoryConnection.h"
#include "CheckRecipSigner.h"
#include "DBClientClasses.h"
#include "FastLock.h"
#include "HeadlessContainer.h"
#include "RequestReplyCommand.h"
#include "TradeSettings.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"

using namespace Blocksettle::Communication;


AuthAddressManager::AuthAddressManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ArmoryConnection> &armory)
   : QObject(nullptr), logger_(logger), armory_(armory), authCT_(this)
{}

AuthAddressManager::AuthAddressManager(const std::shared_ptr<spdlog::logger>& logger
   , AuthCallbackTarget *act)
   : logger_(logger), authCT_(act)
{}

std::shared_ptr<bs::TradeSettings> AuthAddressManager::tradeSettings() const
{
   return tradeSettings_;
}

void AuthAddressManager::SetAuthWallet()
{
   authWallet_ = walletsManager_->getAuthWallet();
}

bool AuthAddressManager::setup()
{
   if (!HaveAuthWallet()) {
      logger_->debug("[AuthAddressManager::setup] Auth wallet missing");
      addressVerificator_.reset();
      return false;
   }
   if (addressVerificator_) {
      return true;
   }

   if (readyError() != ReadyError::NoError) {
      return false;
   }

   if (armory_) {
      addressVerificator_ = std::make_shared<AddressVerificator>(logger_, armory_
         , [this](const bs::Address& address, AddressVerificationState state)
      {
         if (!addressVerificator_) {
            logger_->error("[AuthAddressManager::setup] Failed to create AddressVerificator object");
            return;
         }

         logger_->info("Address verification on chain {} for {}", to_string(state), address.display());
         SetValidationState(address, state);
      });
   }
   SetBSAddressList(bsAddressList_);
   return true;
}

void AuthAddressManager::onAuthWalletChanged()
{
   SetAuthWallet();
   addresses_.clear();
   tryVerifyWalletAddresses();
   authCT_->authWalletChanged();
}

AuthAddressManager::~AuthAddressManager() noexcept
{
   addressVerificator_.reset();
   ArmoryCallbackTarget::cleanup();
}

size_t AuthAddressManager::GetAddressCount()
{
   FastLock locker(lockList_);
   return addresses_.size();
}

bs::Address AuthAddressManager::GetAddress(size_t index)
{
   FastLock locker(lockList_);
   if (index >= addresses_.size()) {
      return {};
   }
   return addresses_[index];
}

bool AuthAddressManager::WalletAddressesLoaded()
{
   FastLock locker(lockList_);
   return !addresses_.empty();
}

AuthAddressManager::ReadyError AuthAddressManager::readyError() const
{
   if (!HasAuthAddr()) {
      return ReadyError::MissingAuthAddr;
   }
   if (!HaveBSAddressList()) {
      return ReadyError::MissingAddressList;
   }
   if (!armory_ || !armory_->isOnline()) {
      return ReadyError::ArmoryOffline;
   }

   return ReadyError::NoError;
}

bool AuthAddressManager::HaveAuthWallet() const
{
   return (authWallet_ != nullptr);
}

bool AuthAddressManager::HasAuthAddr() const
{
   return (HaveAuthWallet() && (authWallet_->getUsedAddressCount() > 0));
}

bool AuthAddressManager::CreateNewAuthAddress()
{
   const auto &cbCreateAddress = [this]
   {
      if (!authWallet_) {
         logger_->error("[AuthAddressManager::CreateNewAuthAddress] no auth leaf");
         return;
      }
      const auto &cbAddr = [this](const bs::Address &) {
         emit walletsManager_->walletChanged(authWallet_->walletId());
      };
      authWallet_->getNewExtAddress(cbAddr);
   };
   if (HaveAuthWallet()) {
      cbCreateAddress();
   }
   else {
      return walletsManager_->createAuthLeaf(cbCreateAddress);
   }
   return true;
}

void AuthAddressManager::onTXSigned(unsigned int id, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason)
{
   const auto &itRevoke = signIdsRevoke_.find(id);
   if (itRevoke == signIdsRevoke_.end()) {
      return;
   }
   signIdsRevoke_.erase(id);

   if (result == bs::error::ErrorCode::NoError) {
      if (BroadcastTransaction(signedTX)) {
         authCT_->authRevokeTxSent();
      }
      else {
         authCT_->onError(tr("Failed to broadcast transaction").toStdString());
      }
   }
   else {
      logger_->error("[AuthAddressManager::onTXSigned] TX signing failed: {} {}"
         , bs::error::ErrorCodeToString(result).toStdString(), errorReason);
      authCT_->onError(tr("Transaction sign error: %1").arg(bs::error::ErrorCodeToString(result)).toStdString());
   }
}

bool AuthAddressManager::RevokeAddress(const bs::Address &address)
{
   const auto state = GetState(address);
   if ((state != AuthAddressState::Verifying) && (state != AuthAddressState::Verified)) {
      logger_->warn("[AuthAddressManager::RevokeAddress] attempting to revoke from incorrect state {}", (int)state);
      authCT_->onError(tr("incorrect state").toStdString());
      return false;
   }
   if (!signingContainer_) {
      logger_->error("[AuthAddressManager::RevokeAddress] can't revoke without signing container");
      authCT_->onError(tr("Missing signing container").toStdString());
      return false;
   }

   if (!addressVerificator_) {
      SPDLOG_LOGGER_ERROR(logger_, "addressVerificator_ is null");
      authCT_->onError(tr("Missing address verificator").toStdString());
      return false;
   }

   const auto revokeData = addressVerificator_->getRevokeData(address);
   if (revokeData.first.empty() || !revokeData.second.isInitialized()) {
      logger_->error("[AuthAddressManager::RevokeAddress] failed to obtain revocation data");
      authCT_->onError(tr("Missing revocation input").toStdString());
      return false;
   }

   const auto reqId = signingContainer_->signAuthRevocation(authWallet_->walletId(), address
      , revokeData.second, revokeData.first);
   if (!reqId) {
      logger_->error("[AuthAddressManager::RevokeAddress] failed to send revocation data");
      authCT_->onError(tr("Failed to send revoke").toStdString());
      return false;
   }
   signIdsRevoke_.insert(reqId);
   return true;
}

#if 0 // Auth eID and BsClient are deprecated
void AuthAddressManager::ConfirmSubmitForVerification(const std::weak_ptr<BsClient> &bsClient, const bs::Address &address)
{
   logger_->debug("[AuthAddressManager::ConfirmSubmitForVerification] confirm submission of {}", address.display());

   auto bsClientPtr = bsClient.lock();
   bsClientPtr->signAuthAddress(address, [this, address, bsClient] (const BsClient::SignResponse &response) {
      if (response.userCancelled) {
         logger_->error("[AuthAddressManager::ConfirmSubmitForVerification sign cb] signing auth address cancelled: {}", response.errorMsg);
         authCT_->authAddressSubmitCancelled(address);
         return;
      }

      if (!response.success) {
         logger_->error("[AuthAddressManager::ConfirmSubmitForVerification sign cb] signing auth address failed: {}", response.errorMsg);
         authCT_->authAddressSubmitError(address, bs::error::AuthAddressSubmitResult::AuthRequestSignFailed);
         return;
      }

      logger_->debug("[AuthAddressManager::ConfirmSubmitForVerification sign cb] signing auth address succeed");

      auto bsClientPtr = bsClient.lock();
      if (!bsClientPtr) {
         logger_->error("[AuthAddressManager::ConfirmSubmitForVerification sign cb] disconnected from server");
         return;
      }

      bsClientPtr->confirmAuthAddress(address, [this, address] (bs::error::AuthAddressSubmitResult submitResult) {
         if (submitResult != bs::error::AuthAddressSubmitResult::Success) {
            logger_->error("[AuthAddressManager::ConfirmSubmitForVerification confirm cb] confirming auth address failed: {}", static_cast<int>(submitResult));
            authCT_->authAddressSubmitError(address, submitResult);
            return;
         }

         logger_->debug("[AuthAddressManager::ConfirmSubmitForVerification confirm cb] confirming auth address succeed");

         markAsSubmitted(address);
      });
   });
}
#endif

void AuthAddressManager::submitToProxy(const bs::Address &address)
{
   //TODO: implement via proxy connection
}

void AuthAddressManager::tryVerifyWalletAddresses()
{
   std::string errorMsg;
   ReadyError state = readyError();
   if (state != ReadyError::NoError) {
      SPDLOG_LOGGER_DEBUG(logger_, "can't start auth address verification: {}", readyErrorStr(state));
      return;
   }

   setup();

   VerifyWalletAddressesFunction();
}

void AuthAddressManager::VerifyWalletAddressesFunction()
{
   logger_->debug("[AuthAddressManager::VerifyWalletAddressesFunction] Starting to VerifyWalletAddresses");

   if (!HaveBSAddressList()) {
      logger_->debug("AuthAddressManager doesn't have BS addresses");
      return;
   }
   bool updated = false;

   if (!WalletAddressesLoaded()) {
      if (authWallet_ != nullptr) {
         for (const auto &addr : authWallet_->getUsedAddressList()) {
            AddAddress(addr);
         }
      }
      else {
         logger_->debug("AuthAddressManager auth wallet is null");
      }
      updated = true;
   }

   std::vector<bs::Address> listCopy;
   {
      FastLock locker(lockList_);
      listCopy = addresses_;
   }

   for (auto &addr : listCopy) {
      addressVerificator_->addAddress(addr);
   }
   addressVerificator_->startAddressVerification();

   if (updated) {
      authCT_->verifiedAddressListUpdated();
      authCT_->addressListUpdated();
   }
}

void AuthAddressManager::ClearAddressList()
{
   bool adressListChanged = false;
   {
      FastLock locker(lockList_);
      if (!addresses_.empty()) {
         addresses_.clear();
         adressListChanged = true;
      }
   }

   if (adressListChanged) {
      authCT_->addressListUpdated();
      authCT_->verifiedAddressListUpdated();
   }
}

void AuthAddressManager::onWalletChanged(const std::string &walletId)
{
   bool listUpdated = false;
   if ((authWallet_ != nullptr) && (walletId == authWallet_->walletId())) {
      const auto &newAddresses = authWallet_->getUsedAddressList();
      const auto count = newAddresses.size();
      listUpdated = (count > addresses_.size());

      for (size_t i = addresses_.size(); i < count; i++) {
         const auto &addr = newAddresses[i];
         AddAddress(addr);
         if (addressVerificator_) {
            addressVerificator_->addAddress(addr);
         }
      }
   }

   if (listUpdated && addressVerificator_) {
      addressVerificator_->startAddressVerification();
      authCT_->addressListUpdated();
   }
}

void AuthAddressManager::AddAddress(const bs::Address &addr)
{
   SetExplicitState(addr, AuthAddressState::Unknown);

   FastLock locker(lockList_);
   addresses_.emplace_back(addr);
}

bool AuthAddressManager::HaveBSAddressList() const
{
   return !bsAddressList_.empty();
}

const std::unordered_set<std::string> &AuthAddressManager::GetBSAddresses() const
{
   return bsAddressList_;
}

std::string AuthAddressManager::readyErrorStr(AuthAddressManager::ReadyError error)
{
   switch (error) {
      case ReadyError::NoError:              return "NoError";
      case ReadyError::MissingAuthAddr:      return "MissingAuthAddr";
      case ReadyError::MissingAddressList:   return "MissingAddressList";
      case ReadyError::MissingArmoryPtr:     return "MissingArmoryPtr";
      case ReadyError::ArmoryOffline:        return "ArmoryOffline";
   }
   return "Unknown";
}

void AuthAddressManager::SetLoadedValidationAddressList(const std::unordered_set<std::string>& validationAddresses)
{
   logger_->debug("[AuthAddressManager::SetLoadedValidationAddressList] get {} BS addresses", validationAddresses.size());

   ClearAddressList();
   SetBSAddressList(validationAddresses);
   tryVerifyWalletAddresses();
}

AuthAddressManager::AuthAddressState AuthAddressManager::GetState(const bs::Address &addr) const
{
   FastLock lock(statesLock_);

   const auto itState = states_.find(addr);
   if (itState == states_.end()) {
      return AuthAddressState::Unknown;
   }

   return itState->second;
}

void AuthAddressManager::SetExplicitState(const bs::Address &addr, AuthAddressState state)
{
   FastLock lock(statesLock_);
   states_[addr] = state;
}

void AuthAddressManager::SetValidationState(const bs::Address &addr, AddressVerificationState state)
{
   const auto prevState = GetState(addr);

   AuthAddressState mappedState = AuthAddressState::Unknown;

   switch(state) {
   case AddressVerificationState::VerificationFailed:
      mappedState = AuthAddressState::Invalid;
      break;
   case AddressVerificationState::Virgin:
      mappedState = AuthAddressState::NotSubmitted;
      break;
   case AddressVerificationState::Tainted:
      mappedState = AuthAddressState::Tainted;
      break;
   case AddressVerificationState::Verifying:
      mappedState = AuthAddressState::Verifying;
      break;
   case AddressVerificationState::Verified:
      mappedState = AuthAddressState::Verified;
      break;
   case AddressVerificationState::Revoked:
      mappedState = AuthAddressState::Revoked;
      break;
   case AddressVerificationState::Invalidated_Explicit:
   case AddressVerificationState::Invalidated_Implicit:
      mappedState = AuthAddressState::RevokedByBS;
      break;
   }

   if (prevState == mappedState) {
      return;
   }

   if (mappedState == AuthAddressState::NotSubmitted) {
      if (prevState == AuthAddressState::Submitted) {
         return;
      }

      /*const auto& submittedAddresses = celerClient_->GetSubmittedAuthAddressSet();
      if (submittedAddresses.find(addr.display()) != submittedAddresses.end()) {
         mappedState = AuthAddressState::Submitted;
      }*/   //TODO: replace with proxy connection
   }

   SetExplicitState(addr, mappedState);
   bool updateAddrState = true;

   if (mappedState == AuthAddressState::Verified
      && (prevState == AuthAddressState::Verifying
         || prevState == AuthAddressState::Submitted)) {
      authCT_->addrVerifiedOrRevoked(addr, AuthAddressState::Verified);
      authCT_->verifiedAddressListUpdated();
      updateAddrState = false;
   } else if ((mappedState == AuthAddressState::Revoked || mappedState == AuthAddressState::RevokedByBS)
              && (prevState == AuthAddressState::Verified)) {
      authCT_->addrVerifiedOrRevoked(addr, AuthAddressState::Revoked);
      updateAddrState = false;
   }

   if (updateAddrState) {
      authCT_->addrStateChanged(addr, mappedState);
   }
   authCT_->addressListUpdated();
}

bool AuthAddressManager::BroadcastTransaction(const BinaryData& transactionData)
{
   return !armory_->broadcastZC(transactionData).empty();
}

void AuthAddressManager::setDefault(const bs::Address &addr)
{
   defaultAddr_ = addr;
   settings_->set(ApplicationSettings::defaultAuthAddr, QString::fromStdString(addr.display()));
   authCT_->verifiedAddressListUpdated();
}

bs::Address AuthAddressManager::getDefault() const
{
   if (defaultAddr_.empty()) {
      const auto &defaultAuthAddrStr = settings_->get<std::string>(ApplicationSettings::defaultAuthAddr);
      if (!defaultAuthAddrStr.empty()) {
         try {
            defaultAddr_ = bs::Address::fromAddressString(defaultAuthAddrStr);
         }
         catch (const std::exception &e) {
            logger_->error("[AuthAddressManager::getDefault] invalid default address: {}"
               , e.what());
            return {};
         }
      }
      auto verifAddresses = GetVerifiedAddressList();
      if (verifAddresses.empty()) {
         verifAddresses = GetSubmittedAddressList();
      }

      if (verifAddresses.empty()) {
         defaultAddr_.clear();
         return {};
      }
      const auto &it = std::find(verifAddresses.cbegin(), verifAddresses.cend(), defaultAddr_);
      if (defaultAddr_.empty() || (it == verifAddresses.end())) {
         defaultAddr_ = verifAddresses.at(0);
      }
   }
   return defaultAddr_;
}

size_t AuthAddressManager::getDefaultIndex() const
{
   if (defaultAddr_.empty()) {
      return 0;
   }
   size_t rv = 0;
   FastLock locker(lockList_);
   for (const auto& address : addresses_) {
      if (GetState(address) != AuthAddressState::Verified) {
         continue;
      }
      if (address.prefixed() == defaultAddr_.prefixed()) {
         return rv;
      }
      rv++;
   }
   return 0;
}

std::vector<bs::Address> AuthAddressManager::GetSubmittedAddressList(bool includeVerified /* = true */) const
{
   std::vector<bs::Address> list;
   {
      list.reserve(addresses_.size());

      FastLock locker(lockList_);
      for (const auto& address : addresses_) {
         const auto addressState = GetState(address);

         switch (addressState) {
         case AuthAddressState::Verified:
            if (!includeVerified) {
               break;
            }
         case AuthAddressState::Verifying:
         case AuthAddressState::Submitted:
         case AuthAddressState::Tainted:
            list.emplace_back(address);
            break;
         default:
            break;
         }
      }
   }
   return list;
}

std::vector<bs::Address> AuthAddressManager::GetVerifiedAddressList() const
{
   std::vector<bs::Address> list;
   {
      FastLock locker(lockList_);
      for (const auto& address : addresses_) {
         if (GetState(address) == AuthAddressState::Verified) {
            list.emplace_back(address);
         }
      }
   }
   return list;
}

bool AuthAddressManager::isAtLeastOneAwaitingVerification() const
{
   {
      FastLock locker(lockList_);
      for (const auto &address : addresses_) {
         auto addrState = GetState(address);
         if (addrState == AuthAddressState::Verifying
            || addrState == AuthAddressState::Verified) {
            return true;
         }
      }
   }
   return false;
}

bool AuthAddressManager::isAllLoadded() const
{
   {
      FastLock locker(lockList_);
      for (const auto &address : addresses_) {
         auto addrState = GetState(address);
         if (addrState == AuthAddressState::Unknown) {
            return false;
         }
      }
   }
   return true;
}

void AuthAddressManager::SetBSAddressList(const std::unordered_set<std::string>& bsAddressList)
{
   {
      FastLock locker(lockList_);
      bsAddressList_ = bsAddressList;

      if (!bsAddressList.empty()) {
         if (addressVerificator_) {
            addressVerificator_->SetBSAddressList(bsAddressList);
         }
      }
   }

   // Emit signal without holding lock
   authCT_->bsAddressList();
}

void AuthAddressManager::onStateChanged(ArmoryState)
{
   QMetaObject::invokeMethod(this, [this]{
      tryVerifyWalletAddresses();
   });
}

void AuthAddressManager::markAsSubmitted(const bs::Address &address)
{
   submitToProxy(address);

   SetExplicitState(address, AuthAddressState::Submitted);

   authCT_->addressListUpdated();
   authCT_->authAddressSubmitSuccess(address);
}

template <typename TVal> TVal AuthAddressManager::lookup(const bs::Address &key, const std::map<bs::Address, TVal> &container) const
{
   const auto it = container.find(key);
   if (it == container.end()) {
      return TVal();
   }
   return it->second;
}

void AuthAddressManager::onWalletCreated()
{
   auto authLeaf = walletsManager_->getAuthWallet();

   if (authLeaf != nullptr) {
      authCT_->authWalletCreated(authLeaf->walletId());
   } else {
      logger_->error("[AuthAddressManager::onWalletCreated] we should be able to get auth wallet at this point");
   }
}

bool AuthAddressManager::hasSettlementLeaf(const bs::Address &addr) const
{
   return walletsManager_->hasSettlementLeaf(addr);
}

void AuthAddressManager::createSettlementLeaf(const bs::Address &addr
   , const std::function<void()> &cb)
{
   const auto &cbPubKey = [this, cb](const SecureBinaryData &pubKey) {
      if (pubKey.empty()) {
         return;
      }
      if (cb) {
         cb();
      }
   };
   walletsManager_->createSettlementLeaf(addr, cbPubKey);
}

bool AuthAddressManager::UserCanSubmitAuthAddress() const
{
   size_t submittedAddressCount = GetSubmittedAddressList(false).size();

   size_t maxSubmitCount = 0;

   if (userType_ == bs::network::UserType::Dealing) {
      maxSubmitCount = tradeSettings_->dealerAuthSubmitAddressLimit;
   } else if (userType_ == bs::network::UserType::Trading) {
      maxSubmitCount = tradeSettings_->authSubmitAddressLimit;
   }

   return maxSubmitCount > submittedAddressCount;
}

void AuthAddressManager::setUserType(bs::network::UserType userType)
{
   userType_ = userType;
}
