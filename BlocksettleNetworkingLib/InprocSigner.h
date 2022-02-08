/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef INPROC_SIGNER_H
#define INPROC_SIGNER_H

#include "WalletSignerContainer.h"
#include <vector>
#include "CoreHDWallet.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace core {
      namespace hd {
         class Leaf;
         class Wallet;
      }
      class SettlementWallet;
      class WalletsManager;
   }
}


class InprocSigner : public WalletSignerContainer
{
public:
   using PasswordLock = std::unique_ptr<bs::core::WalletPasswordScoped>;
   using PwdLockCb = std::function<PasswordLock(const std::string& walletId)>;

   InprocSigner(const std::shared_ptr<bs::core::WalletsManager> &
      , const std::shared_ptr<spdlog::logger> &, SignerCallbackTarget*
      , const std::string &walletsPath, ::NetworkType, const PwdLockCb& cb = nullptr);
   InprocSigner(const std::shared_ptr<bs::core::hd::Wallet> &, SignerCallbackTarget*
      , const std::shared_ptr<spdlog::logger> &, const PwdLockCb& cb = nullptr);
   ~InprocSigner() noexcept override = default;

   void Start() override;
   bool Stop() override { return true; }
   void Connect() override {}
   bool Disconnect() override { return true; }
   bool isOffline() const override { return false; }
   bool isWalletOffline(const std::string &) const override { return false; }

   void signTXRequest(const bs::core::wallet::TXSignRequest&
      , const std::function<void(const BinaryData &signedTX, bs::error::ErrorCode, const std::string& errorReason)>&
      , TXSignMode mode = TXSignMode::Full, bool keepDuplicatedRecipients = false) override;

   //bs::signer::RequestId signSettlementTXRequest(const bs::core::wallet::TXSignRequest &
   //   , const bs::sync::PasswordDialogData &, TXSignMode, bool
   //   , const std::function<void(bs::error::ErrorCode result, const BinaryData &signedTX)> &) override;

   //bs::signer::RequestId signSettlementPayoutTXRequest(const bs::core::wallet::TXSignRequest &
   //   , const bs::core::wallet::SettlementData &, const bs::sync::PasswordDialogData &
   //   , const std::function<void(bs::error::ErrorCode, const BinaryData &signedTX)> &) override;

   bs::signer::RequestId resolvePublicSpenders(const bs::core::wallet::TXSignRequest &
      , const SignerStateCb &cb) override;

   bs::signer::RequestId CancelSignTx(const BinaryData &tx) override { return 0; }

   // cb is ignored in inproc signer
   bool createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &path
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}
      , bs::sync::PasswordDialogData dialogData = {}, const CreateHDLeafCb &cb = nullptr) override;
   bs::signer::RequestId DeleteHDRoot(const std::string &) override;
   bs::signer::RequestId DeleteHDLeaf(const std::string &) override;
   bs::signer::RequestId GetInfo(const std::string &) override;

   bs::signer::RequestId customDialogRequest(bs::signer::ui::GeneralDialogType, const QVariantMap&) override { return 0; }
   bs::signer::RequestId updateDialogData(const bs::sync::PasswordDialogData &dialogData, uint32_t dialogId = 0) override { return 0; }

   void syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &) override;
   void syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &) override;
   void syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &) override;
   void syncAddressComment(const std::string &walletId, const bs::Address &, const std::string &) override;
   void syncTxComment(const std::string &walletId, const BinaryData &, const std::string &) override;
   void extendAddressChain(const std::string &walletId, unsigned count, bool extInt,
      const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;
   void syncAddressBatch(const std::string &walletId, const std::set<BinaryData>& addrSet,
      std::function<void(bs::sync::SyncState)> cb) override;

   void syncNewAddresses(const std::string &walletId, const std::vector<std::string> &
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &) override;

   //void createSettlementWallet(const bs::Address &authAddr
   //   , const std::function<void(const SecureBinaryData &)> &) override;
   //void setSettlementID(const std::string &walletId, const SecureBinaryData &id
   //   , const std::function<void(bool)> &) override;
   //void getSettlementPayinAddress(const std::string &walletId
   //   , const bs::core::wallet::SettlementData &
   //   , const std::function<void(bool, bs::Address)> &) override;
   void getRootPubkey(const std::string &walletID
      , const std::function<void(bool, const SecureBinaryData &)> &) override;

   //void setSettlAuthAddr(const std::string &walletId, const BinaryData &, const bs::Address &addr) override;
   //void getSettlAuthAddr(const std::string &walletId, const BinaryData &
   //   , const std::function<void(const bs::Address &)> &) override;
   //void setSettlCP(const std::string &walletId, const BinaryData &payinHash, const BinaryData &settlId
   //   , const BinaryData &cpPubKey) override;
   //void getSettlCP(const std::string &walletId, const BinaryData &payinHash
   //   , const std::function<void(const BinaryData &, const BinaryData &)> &) override;

   bool isReady() const override { return inited_; }

private:
   std::shared_ptr<bs::core::hd::Leaf> getAuthLeaf() const;

private:
   std::shared_ptr<bs::core::WalletsManager> walletsMgr_;
   const std::string walletsPath_;
   ::NetworkType     netType_ = ::NetworkType::Invalid;
   bs::signer::RequestId   seqId_ = 1;
   bool           inited_ = false;
   PwdLockCb   pwLockCb_{ nullptr };
};

#endif // INPROC_SIGNER_H
