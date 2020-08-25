/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SignerClient.h"
#include <spdlog/spdlog.h>
#include "Message/Bus.h"
#include "Message/Envelope.h"

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;

SignerClient::SignerClient(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &user)
   : WalletSignerContainer(logger, OpMode::LocalInproc)
   , logger_(logger), signerUser_(user)
{}

bool SignerClient::isSignerUser(const std::shared_ptr<bs::message::User> &user) const
{
   return (user && (user->value() == signerUser_->value()));
}

bool SignerClient::process(const Envelope &env)
{
   SignerMessage msg;
   if (!msg.ParseFromString(env.message)) {
      logger_->error("[{}] {} is not a signer message", __func__, env.id);
      return true;
   }
   switch (msg.data_case()) {
   case SignerMessage::kReady:
      if (cbReady_) {
         cbReady_();
      }
      break;
   case SignerMessage::kWalletsListUpdated:
      if (cbWalletsListUpdated_) {
         cbWalletsListUpdated_();
      }
      break;
   case SignerMessage::kWalletsInfo:
      return processWalletsInfo(env.id, msg.wallets_info());
   default:
      logger_->debug("[{}] unknown signer message {}", __func__, msg.data_case());
      break;
   }
   return true;
}

bool SignerClient::processWalletsInfo(uint64_t msgId, const SignerMessage_WalletsInfo &response)
{
   const auto &itReq = reqSyncWalletMap_.find(msgId);
   if (itReq == reqSyncWalletMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   std::vector<bs::sync::WalletInfo> wi;
   for (const auto &wallet : response.wallets()) {
      bs::sync::WalletInfo entry;
      entry.format = static_cast<bs::sync::WalletFormat>(wallet.format());
      entry.id = wallet.id();
      entry.name = wallet.name();
      entry.description = wallet.description();
      entry.netType = static_cast<NetworkType>(wallet.network_type());
      entry.watchOnly = wallet.watch_only();
      for (const auto &encType : wallet.encryption_types()) {
         entry.encryptionTypes.push_back(static_cast<bs::wallet::EncryptionType>(encType));
      }
      for (const auto &encKey : wallet.encryption_keys()) {
         entry.encryptionKeys.push_back(BinaryData::fromString(encKey));
      }
      entry.encryptionRank = { wallet.encryption_rank().m(), wallet.encryption_rank().n() };
      wi.emplace_back(std::move(entry));
   }
   itReq->second(wi);
   reqSyncWalletMap_.erase(itReq);
   return true;
}

void SignerClient::syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &cb)
{
   SignerMessage msg;
   msg.mutable_start_wallets_sync();
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncWalletMap_[env.id] = cb;
}

void SignerClient::syncAddressBatch(const std::string &walletId,
   const std::set<BinaryData>& addrSet, std::function<void(bs::sync::SyncState)> cb)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_sync_addresses();
   msgReq->set_wallet_id(walletId);
   for (const auto &addr : addrSet) {
      msgReq->add_addresses(addr.toBinStr());
   }
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncAddrMap_[env.id] = { walletId, std::move(cb) };
}

void SignerClient::syncNewAddress(const std::string &walletId, const std::string &index
   , const std::function<void(const bs::Address &)> &)
{
}

void SignerClient::syncNewAddresses(const std::string &walletId, const std::vector<std::string> &
   , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &, bool persistent)
{
}

void SignerClient::syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &)
{
}

void SignerClient::syncAddressComment(const std::string &walletId, const bs::Address &, const std::string &)
{
}

void SignerClient::syncTxComment(const std::string &walletId, const BinaryData &, const std::string &)
{
}

void SignerClient::extendAddressChain(const std::string &walletId, unsigned count, bool extInt,
   const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &)
{
}

void SignerClient::setSettlementID(const std::string &walletId, const SecureBinaryData &id
   , const std::function<void(bool)> &)
{
}

void SignerClient::getRootPubkey(const std::string &walletID
   , const std::function<void(bool, const SecureBinaryData &)> &)
{
}
