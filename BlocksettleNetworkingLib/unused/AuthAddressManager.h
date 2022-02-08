/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __AUTH_ADDRESS_MANAGER_H__
#define __AUTH_ADDRESS_MANAGER_H__

#include "AuthAddress.h"

#include <atomic>
#include <memory>
#include <set>
#include <unordered_set>
#include <vector>
#include <QObject>
#include "Address.h"
#include "ArmoryConnection.h"
#include "BSErrorCode.h"
#include "BSErrorCodeStrings.h"
#include "CommonTypes.h"
#include "WalletEncryption.h"

#include "bs_communication.pb.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      namespace hd {
         class Leaf;
         class SettlementLeaf;
      }
      class Wallet;
      class WalletsManager;
   }
   struct TradeSettings;
}
class AddressVerificator;
class ApplicationSettings;
class ArmoryConnection;
class HeadlessContainer;
class RequestReplyCommand;
class ResolverFeed_AuthAddress;


struct AuthCallbackTarget
{
   enum class AuthAddressState : int
   {
      Unknown, // in progress
      NotSubmitted,
      Submitted,
      Tainted,
      Verifying,
      Verified,
      Revoked,
      RevokedByBS,
      Invalid
   };

   virtual void addressListUpdated() {}
   virtual void verifiedAddressListUpdated() {}
   virtual void addrVerifiedOrRevoked(const bs::Address&, AuthAddressState) {}
   virtual void addrStateChanged(const bs::Address&, AuthAddressState) {}
   virtual void authWalletChanged() {}
   virtual void authWalletCreated(const std::string& walletId) {}
   virtual void onError(const std::string& errorText) const {}
   virtual void onInfo(const std::string& info) {}

   virtual void authAddressSubmitError(const bs::Address& address
      , bs::error::AuthAddressSubmitResult statusCode) {}
   virtual void authAddressSubmitSuccess(const bs::Address& address) {}
   virtual void authAddressSubmitCancelled(const bs::Address& address) {}
   virtual void authRevokeTxSent() {}
   virtual void bsAddressList() {}
};

class AuthAddressManager : public QObject, public ArmoryCallbackTarget, public AuthCallbackTarget
{
   Q_OBJECT

public:
   enum class ReadyError
   {
      NoError,
      MissingAuthAddr,
      MissingAddressList,
      MissingArmoryPtr,
      ArmoryOffline,
   };

   [[deprecated]] AuthAddressManager(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<ArmoryConnection> &);
   AuthAddressManager(const std::shared_ptr<spdlog::logger>&, AuthCallbackTarget*);
   ~AuthAddressManager() noexcept override;

   AuthAddressManager(const AuthAddressManager&) = delete;
   AuthAddressManager& operator = (const AuthAddressManager&) = delete;
   AuthAddressManager(AuthAddressManager&&) = delete;
   AuthAddressManager& operator = (AuthAddressManager&&) = delete;

   std::shared_ptr<bs::TradeSettings> tradeSettings() const;

   size_t GetAddressCount();
   bs::Address GetAddress(size_t index);

   AuthAddressState GetState(const bs::Address &addr) const;

   void setDefault(const bs::Address &addr);

   [[nodiscard]] bs::Address getDefault() const;
   size_t getDefaultIndex() const;

   bool HaveAuthWallet() const;
   bool HasAuthAddr() const;

   bool CreateNewAuthAddress();

   bool hasSettlementLeaf(const bs::Address &) const;
   void createSettlementLeaf(const bs::Address &, const std::function<void()> &);

//   void ConfirmSubmitForVerification(const std::weak_ptr<BsClient> &bsClient, const bs::Address &address);

   bool RevokeAddress(const bs::Address &address);

   ReadyError readyError() const;

   std::vector<bs::Address> GetSubmittedAddressList(bool includeVerified = true) const;

   bool isAtLeastOneAwaitingVerification() const;
   bool isAllLoadded() const;
   const std::unordered_set<std::string> &GetBSAddresses() const;

   static std::string readyErrorStr(ReadyError error);

   void setUserType(bs::network::UserType userType);

   bool UserCanSubmitAuthAddress() const;

   void SetLoadedValidationAddressList(const std::unordered_set<std::string>& validationAddresses);

public slots:  // will turn into just public methods later
   void tryVerifyWalletAddresses();
   void onAuthWalletChanged();
   void onWalletChanged(const std::string &walletId);
   void onTXSigned(unsigned int id, BinaryData signedTX, bs::error::ErrorCode result, const std::string &errorReason);

   void onWalletCreated();

signals:
   void AddressListUpdated();
   void VerifiedAddressListUpdated();
   void AddrVerifiedOrRevoked(const QString &addr, int);
   void AddrStateChanged();
   void AuthWalletChanged();
   void AuthWalletCreated(const QString &walletId);
   void Error(const QString &errorText) const;
   void Info(const QString &info);

   void AuthAddressSubmitError(const QString &address, const bs::error::AuthAddressSubmitResult statusCode);
   void AuthAddressSubmitSuccess(const QString &address);
   void AuthAddressSubmitCancelled(const QString &address);
   void AuthRevokeTxSent();
   void gotBsAddressList();

private: // Auth callbacks override
   void addressListUpdated() override { emit AddressListUpdated(); }
   void verifiedAddressListUpdated() override { emit VerifiedAddressListUpdated(); }
   void addrVerifiedOrRevoked(const bs::Address& addr, AuthAddressState state) override {
      emit AddrVerifiedOrRevoked(QString::fromStdString(addr.display()), (int)state);
      addrStateChanged(addr, state);
   }
   void addrStateChanged(const bs::Address&, AuthAddressState) override { emit AddrStateChanged(); }
   void authWalletChanged() override { emit AuthWalletChanged(); }
   void authWalletCreated(const std::string& walletId) override { emit AuthWalletCreated(QString::fromStdString(walletId)); }
   void onError(const std::string& errorText) const override { emit Error(QString::fromStdString(errorText)); }
   void onInfo(const std::string& info) override { emit Info(QString::fromStdString(info)); }

   void authAddressSubmitError(const bs::Address& address
      , bs::error::AuthAddressSubmitResult statusCode) override {
      emit AuthAddressSubmitError(QString::fromStdString(address.display()), statusCode);
   }
   void authAddressSubmitSuccess(const bs::Address& address) override { emit AuthAddressSubmitSuccess(QString::fromStdString(address.display())); }
   void authAddressSubmitCancelled(const bs::Address& address) override { emit AuthAddressSubmitCancelled(QString::fromStdString(address.display())); }
   void authRevokeTxSent() override { emit AuthRevokeTxSent(); }
   void bsAddressList() override { emit gotBsAddressList(); }

   void SetAuthWallet();
   void ClearAddressList();
   bool setup();

   bool HaveBSAddressList() const;

   void VerifyWalletAddressesFunction();
   bool WalletAddressesLoaded();
   void AddAddress(const bs::Address &addr);

   template <typename TVal> TVal lookup(const bs::Address &key, const std::map<bs::Address, TVal> &container) const;

   void submitToProxy(const bs::Address &);
   bool BroadcastTransaction(const BinaryData& transactionData);
   void SetBSAddressList(const std::unordered_set<std::string>& bsAddressList);

   // From ArmoryCallbackTarget
   void onStateChanged(ArmoryState) override;

   void markAsSubmitted(const bs::Address &address);

   std::vector<bs::Address> GetVerifiedAddressList() const;

   void SetValidationState(const bs::Address &addr, AddressVerificationState state);

   void SetExplicitState(const bs::Address &addr, AuthAddressState state);

protected:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<ArmoryConnection>      armory_;
   std::shared_ptr<ApplicationSettings>   settings_;
   std::shared_ptr<bs::sync::WalletsManager> walletsManager_;
   std::shared_ptr<AddressVerificator>    addressVerificator_;
   AuthCallbackTarget* authCT_{ nullptr };

   mutable std::atomic_flag                  lockList_ = ATOMIC_FLAG_INIT;
   std::vector<bs::Address>                  addresses_;

   std::map<bs::Address, AuthAddressState>   states_;
   mutable std::atomic_flag                  statesLock_ = ATOMIC_FLAG_INIT;

   using HashMap = std::map<bs::Address, BinaryData>;
   mutable bs::Address  defaultAddr_{};

   std::unordered_set<std::string>           bsAddressList_;
   std::shared_ptr<bs::sync::Wallet>         authWallet_;

   std::shared_ptr<HeadlessContainer>  signingContainer_;
   std::unordered_set<unsigned int>    signIdsRevoke_;
   std::shared_ptr<bs::TradeSettings>  tradeSettings_;

   bs::network::UserType userType_ = bs::network::UserType::Undefined;
};

#endif // __AUTH_ADDRESS_MANAGER_H__
