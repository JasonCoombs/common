/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CCFileManager.h"

#include "ApplicationSettings.h"
#include "CelerClient.h"
#include "ConnectionManager.h"
#include "EncryptionUtils.h"
#include "HDPath.h"

#include <spdlog/spdlog.h>

#include <cassert>

#include <QFile>

CCFileManager::CCFileManager(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<ApplicationSettings> &appSettings)
   : logger_{logger}
   , cct_(this)
{
   const auto &cbSecLoaded = [this](const bs::network::CCSecurityDef &ccSecDef) {
      cct_->onCCSecurityDef(ccSecDef);
      cct_->onCCSecurityId(ccSecDef.securityId);
      cct_->onCCSecurityInfo(ccSecDef.product, (unsigned long)ccSecDef.nbSatoshis
         , ccSecDef.genesisAddr);
   };
   const auto &cbLoadComplete = [this] {
      logger_->debug("[CCFileManager] loading complete");
      cct_->onLoaded();
   };
   resolver_ = std::make_shared<CCPubResolver>(logger_
      , appSettings->GetBlocksettleSignAddress()
      , cbSecLoaded, cbLoadComplete);
}

CCFileManager::CCFileManager(const std::shared_ptr<spdlog::logger>& logger
   , CCCallbackTarget* cct, const std::string& signAddress)
   : logger_(logger), cct_(cct)
{
   const auto& cbSecLoaded = [this](const bs::network::CCSecurityDef& ccSecDef) {
      cct_->onCCSecurityDef(ccSecDef);
      cct_->onCCSecurityId(ccSecDef.securityId);
      cct_->onCCSecurityInfo(ccSecDef.product, (unsigned long)ccSecDef.nbSatoshis
         , ccSecDef.genesisAddr);
   };
   const auto& cbLoadComplete = [this] {
      logger_->debug("[CCFileManager] loading complete");
      cct_->onLoaded();
   };
   resolver_ = std::make_shared<CCPubResolver>(logger_, signAddress
      , cbSecLoaded, cbLoadComplete);
}

void CCFileManager::setBsClient(const std::weak_ptr<BsClient> &bsClient)
{
   bsClient_ = bsClient;
}

void CCFileManager::ConnectToCelerClient(const std::shared_ptr<BaseCelerClient> &celerClient)
{
   celerClient_ = celerClient;
}

bool CCFileManager::wasAddressSubmitted(const bs::Address &addr)
{
   return celerClient_->IsCCAddressSubmitted(addr.display());
}

void CCFileManager::cancelActiveSign()
{
   auto bsClient = bsClient_.lock();
   if (bsClient) {
      bsClient->cancelActiveSign();
   }
}

void CCFileManager::SetLoadedDefinitions(const std::vector<bs::network::CCSecurityDef>& definitions)
{
   resolver_->fillFrom(definitions);
}

bool CCFileManager::submitAddress(const bs::Address &address, uint32_t seed, const std::string &ccProduct)
{
   auto bsClient = bsClient_.lock();

   if (!celerClient_) {
      logger_->error("[CCFileManager::SubmitAddressToPuB] not connected");
      return false;
   }

   if (!bsClient) {
      SPDLOG_LOGGER_ERROR(logger_, "not connected to BsProxy");
      return false;
   }

   if (!address.isValid()) {
      SPDLOG_LOGGER_ERROR(logger_, "can't submit invalid CC address: '{}'", address.display());
      return false;
   }

   bsClient->submitCcAddress(address, seed, ccProduct, [this, address](const BsClient::BasicResponse &result) {
      auto bsClient = bsClient_.lock();

      if (!result.success) {
         SPDLOG_LOGGER_ERROR(logger_, "submit CC address failed: '{}'", result.errorMsg);
         cct_->onCCSubmitFailed(address, result.errorMsg);
         return;
      }
      cct_->onCCInitialSubmitted(address);

      if (!bsClient) {
         SPDLOG_LOGGER_ERROR(logger_, "disconnected from server");
         return;
      }

      bsClient->signCcAddress(address, [this, address](const BsClient::SignResponse &result) {
         auto bsClient = bsClient_.lock();

         if (result.userCancelled) {
            SPDLOG_LOGGER_DEBUG(logger_, "signing CC address cancelled: '{}'", result.errorMsg);
            cct_->onCCSubmitFailed(address, tr("Cancelled").toStdString());
            return;
         }

         if (!result.success) {
            SPDLOG_LOGGER_ERROR(logger_, "signing CC address failed: '{}'", result.errorMsg);
            cct_->onCCSubmitFailed(address, result.errorMsg);
            return;
         }

         if (!bsClient) {
            SPDLOG_LOGGER_ERROR(logger_, "disconnected from server");
            return;
         }

         bsClient->confirmCcAddress(address, [this, address](const BsClient::BasicResponse &result) {
            if (!result.success) {
               SPDLOG_LOGGER_ERROR(logger_, "confirming CC address failed: '{}'", result.errorMsg);
               cct_->onCCSubmitFailed(address, result.errorMsg);
               return;
            }

            if (!celerClient_->SetCCAddressSubmitted(address.display())) {
               SPDLOG_LOGGER_WARN(logger_, "failed to save address {} request event to Celer's user storage"
                  , address.display());
            }

            cct_->onCCAddressSubmitted(address);
         });
      });
   });

   return true;
}

void CCPubResolver::clear()
{
   securities_.clear();
   walletIdxMap_.clear();
}

void CCPubResolver::add(const bs::network::CCSecurityDef &ccDef)
{
   securities_[ccDef.product] = ccDef;
   const auto walletIdx = bs::hd::Path::keyToElem(ccDef.product) | bs::hd::hardFlag;
   walletIdxMap_[walletIdx] = ccDef.product;
   cbSecLoaded_(ccDef);
}

std::vector<std::string> CCPubResolver::securities() const
{
   std::vector<std::string> result;
   for (const auto &ccDef : securities_) {
      result.push_back(ccDef.first);
   }
   return result;
}

std::string CCPubResolver::nameByWalletIndex(bs::hd::Path::Elem idx) const
{
   idx |= bs::hd::hardFlag;
   const auto &itWallet = walletIdxMap_.find(idx);
   if (itWallet != walletIdxMap_.end()) {
      return itWallet->second;
   }
   return {};
}

uint64_t CCPubResolver::lotSizeFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.nbSatoshis;
   }
   return 0;
}

bs::Address CCPubResolver::genesisAddrFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.genesisAddr;
   }
   return {};
}

void CCPubResolver::fillFrom(const std::vector<bs::network::CCSecurityDef>& definitions)
{
   clear();
   for (const auto &ccSecDef : definitions) {
      add(ccSecDef);
   }
   cbLoadComplete_();
}
