/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef SIGNER_CLIENT_H
#define SIGNER_CLIENT_H

#include <functional>
#include <memory>
#include "Message/Envelope.h"
#include "WalletSignerContainer.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace message {
      class QueueInterface;
   }
}
namespace BlockSettle {
   namespace Common {
      class HDWalletData;
      class SignerMessage_NewAddressesSynced;
      class SignerMessage_SyncAddrResult;
      class SignerMessage_WalletData;
      class SignerMessage_WalletsInfo;
      class SignerMessage_RootPubKey;
   }
}

class SignerClient : public WalletSignerContainer
{
public:
   SignerClient(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::message::User> &);
   ~SignerClient() override = default;

   bs::signer::RequestId signTXRequest(const bs::core::wallet::TXSignRequest &
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false) override { return 0; }

   bs::signer::RequestId signSettlementTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::sync::PasswordDialogData &dialogData
      , TXSignMode mode = TXSignMode::Full
      , bool keepDuplicatedRecipients = false
      , const std::function<void(bs::error::ErrorCode result, const BinaryData &signedTX)> &cb = nullptr) override { return 0; }

   bs::signer::RequestId signSettlementPartialTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::sync::PasswordDialogData &dialogData
      , const SignTxCb &cb = nullptr) override { return 0; }

   bs::signer::RequestId signSettlementPayoutTXRequest(const bs::core::wallet::TXSignRequest &
      , const bs::core::wallet::SettlementData &, const bs::sync::PasswordDialogData &dialogData
      , const SignTxCb &cb = nullptr) override { return 0; }

   bs::signer::RequestId signAuthRevocation(const std::string &walletId, const bs::Address &authAddr
      , const UTXO &, const bs::Address &bsAddr, const SignTxCb &cb = nullptr) override { return 0; }

   bs::signer::RequestId resolvePublicSpenders(const bs::core::wallet::TXSignRequest &
      , const SignerStateCb &cb) override { return 0; }

   bs::signer::RequestId updateDialogData(const bs::sync::PasswordDialogData &dialogData, uint32_t dialogId = 0) override { return 0; }

   bs::signer::RequestId CancelSignTx(const BinaryData &txId) override { return 0; }

   bs::signer::RequestId setUserId(const BinaryData &, const std::string &walletId) override { return 0; }
   bs::signer::RequestId syncCCNames(const std::vector<std::string> &) override { return 0; }

   bs::signer::RequestId GetInfo(const std::string &rootWalletId) override { return 0; }

   bs::signer::RequestId customDialogRequest(bs::signer::ui::GeneralDialogType signerDialog
      , const QVariantMap &data = QVariantMap()) override { return 0; }

   void syncNewAddress(const std::string &walletId, const std::string &index
      , const std::function<void(const bs::Address &)> &) override;
   void syncNewAddresses(const std::string &walletId, const std::vector<std::string> &
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;

   void syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &) override;
   void syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &) override;
   void syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &) override;
   void syncAddressComment(const std::string &walletId, const bs::Address &, const std::string &) override;
   void syncTxComment(const std::string &walletId, const BinaryData &, const std::string &) override;

   void setSettlAuthAddr(const std::string &walletId, const BinaryData &, const bs::Address &addr) override {}
   void getSettlAuthAddr(const std::string &walletId, const BinaryData &
      , const std::function<void(const bs::Address &)> &) override {}
   void setSettlCP(const std::string &walletId, const BinaryData &payinHash, const BinaryData &settlId
      , const BinaryData &cpPubKey) override {}
   void getSettlCP(const std::string &walletId, const BinaryData &payinHash
      , const std::function<void(const BinaryData &, const BinaryData &)> &) override {}

   void syncAddressBatch(const std::string &walletId,
      const std::set<BinaryData>& addrSet, std::function<void(bs::sync::SyncState)>) override;
   void extendAddressChain(const std::string &walletId, unsigned count, bool extInt,
      const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;

   bs::signer::RequestId DeleteHDRoot(const std::string &rootWalletId) override;
   bs::signer::RequestId DeleteHDLeaf(const std::string &leafWalletId) override;

   //settlement related methods
   void createSettlementWallet(const bs::Address &authAddr
      , const std::function<void(const SecureBinaryData &)> &) override {}
   void setSettlementID(const std::string &walletId, const SecureBinaryData &id
      , const std::function<void(bool)> &) override;
   void getSettlementPayinAddress(const std::string &walletID
      , const bs::core::wallet::SettlementData &
      , const std::function<void(bool, bs::Address)> &) override {}
   void getRootPubkey(const std::string &walletID
      , const std::function<void(bool, const SecureBinaryData &)> &) override;

   void getChatNode(const std::string &walletID
      , const std::function<void(const BIP32_Node &)> &) override {}

   bool createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}
   , bs::sync::PasswordDialogData dialogData = {}, const CreateHDLeafCb &cb = nullptr) override { return false; }

   bool promoteHDWallet(const std::string& rootWalletId, const BinaryData &userId
      , bs::sync::PasswordDialogData dialogData = {}, const PromoteHDWalletCb& cb = nullptr) override { return false; }

   virtual bool isSignerUser(const std::shared_ptr<bs::message::User> &) const;
   virtual bool process(const bs::message::Envelope &);

   void setClientUser(const std::shared_ptr<bs::message::User> &user) { clientUser_ = user; }
   void setQueue(const std::shared_ptr<bs::message::QueueInterface> &queue) { queue_ = queue; }

   void setSignerReady(const std::function<void()> &cb) { cbReady_ = cb; }
   void setWalletsLoaded(const std::function<void()> &cb) { cbWalletsReady_ = cb; }
   void setNoWalletsFound(const std::function<void()> &cb) { cbNoWallets_ = cb; }
   void setWalletsListUpdated(const std::function<void()> &cb) { cbWalletsListUpdated_ = cb; }

private:
   bool processWalletsInfo(uint64_t msgId, const BlockSettle::Common::SignerMessage_WalletsInfo &);
   bool processSyncAddr(uint64_t msgId, const BlockSettle::Common::SignerMessage_SyncAddrResult &);
   bool processNewAddresses(uint64_t msgId, const BlockSettle::Common::SignerMessage_NewAddressesSynced &);
   bool processWalletSync(uint64_t msgId, const BlockSettle::Common::SignerMessage_WalletData &);
   bool processHdWalletSync(uint64_t msgId, const BlockSettle::Common::HDWalletData &);
   bool processSetSettlId(uint64_t msgId, bool);
   bool processRootPubKey(uint64_t msgId, const BlockSettle::Common::SignerMessage_RootPubKey &);

private:
   std::shared_ptr<spdlog::logger>     logger_;
   std::shared_ptr<bs::message::User>  signerUser_, clientUser_;
   std::shared_ptr<bs::message::QueueInterface> queue_;

   std::function<void()>   cbReady_{ nullptr };
   std::function<void()>   cbWalletsReady_{ nullptr };
   std::function<void()>   cbNoWallets_{ nullptr };
   std::function<void()>   cbWalletsListUpdated_{ nullptr };

   std::map<uint64_t, std::function<void(std::vector<bs::sync::WalletInfo>)>>    reqSyncWalletInfoMap_;
   std::map<uint64_t, std::pair<std::string, std::function<void(bs::sync::SyncState)>>>   reqSyncAddrMap_;
   std::map<uint64_t, std::function<void(const bs::Address &)>>   reqSyncNewAddrSingle_;
   std::map<uint64_t, std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>>  reqSyncNewAddrMulti_;
   std::map<uint64_t, std::function<void(bs::sync::WalletData)>>     reqSyncWalletMap_;
   std::map<uint64_t, std::function<void(bs::sync::HDWalletData)>>   reqSyncHdWalletMap_;
   std::map<uint64_t, std::function<void(bool)>>                  reqSettlIdMap_;
   std::map<uint64_t, std::function<void(bool, const SecureBinaryData &)>> reqPubKeyMap_;
};


#endif	// SIGNER_CLIENT_H
