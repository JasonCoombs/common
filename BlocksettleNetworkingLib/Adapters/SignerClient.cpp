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
   case SignerMessage::kState:
      if ((static_cast<SignContainer::ConnectionError>(msg.state().code()) == 
         SignContainer::Ready) && cbReady_) {
         cbReady_();
      }
      break;
   case SignerMessage::kWalletsListUpdated:
      if (cbWalletsListUpdated_) {
         cbWalletsListUpdated_();
      }
      break;
   case SignerMessage::kNeedNewWalletPrompt:
      if (cbNoWallets_) {
         cbNoWallets_();
      }
      break;
   case SignerMessage::kWalletsReadyToSync:
      if (cbWalletsReady_) {
         cbWalletsReady_();
      }
      break;
   case SignerMessage::kWalletsInfo:
      return processWalletsInfo(env.id, msg.wallets_info());
   case SignerMessage::kSyncAddrResult:
      return processSyncAddr(env.id, msg.sync_addr_result());
   case SignerMessage::kNewAddresses:
      return processNewAddresses(env.id, msg.new_addresses());
   case SignerMessage::kAddrChainExtended:
      return processNewAddresses(env.id, msg.addr_chain_extended());
   case SignerMessage::kWalletSynced:
      return processWalletSync(env.id, msg.wallet_synced());
   case SignerMessage::kHdWalletSynced:
      return processHdWalletSync(env.id, msg.hd_wallet_synced());
   case SignerMessage::kSettlIdSet:
      return processSetSettlId(env.id, msg.settl_id_set());
   case SignerMessage::kRootPubkey:
      return processRootPubKey(env.id, msg.root_pubkey());
   case SignerMessage::kWindowVisibleChanged:
      break;
   default:
      logger_->debug("[{}] unknown signer message {}", __func__, msg.data_case());
      break;
   }
   return true;
}

bool SignerClient::processWalletsInfo(uint64_t msgId, const SignerMessage_WalletsInfo &response)
{
   const auto &itReq = reqSyncWalletInfoMap_.find(msgId);
   if (itReq == reqSyncWalletInfoMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   std::vector<bs::sync::WalletInfo> wi;
   for (const auto &wallet : response.wallets()) {
      bs::sync::WalletInfo entry;
      entry.format = static_cast<bs::sync::WalletFormat>(wallet.format());
      for (const auto &id : wallet.ids()) {
         entry.ids.push_back(id);
      }
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
   reqSyncWalletInfoMap_.erase(itReq);
   return true;
}

bool SignerClient::processSyncAddr(uint64_t msgId, const SignerMessage_SyncAddrResult &response)
{
   const auto &itReq = reqSyncAddrMap_.find(msgId);
   if (itReq == reqSyncAddrMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   itReq->second.second(static_cast<bs::sync::SyncState>(response.status()));
   reqSyncAddrMap_.erase(itReq);
   return true;
}

bool SignerClient::processNewAddresses(uint64_t msgId, const SignerMessage_NewAddressesSynced &response)
{
   const auto &itReqSingle = reqSyncNewAddrSingle_.find(msgId);
   if (itReqSingle != reqSyncNewAddrSingle_.end()) {
      if (response.addresses_size() != 1) {
         logger_->error("[{}] invalid address count for single reply", __func__);
         return true;
      }
      try {
         const auto &addr = bs::Address::fromAddressString(response.addresses(0).address());
         itReqSingle->second(addr);
      }
      catch (const std::exception &) {}
      reqSyncNewAddrSingle_.erase(itReqSingle);
      return true;
   }

   const auto &itReqMulti = reqSyncNewAddrMulti_.find(msgId);
   if (itReqMulti == reqSyncNewAddrMulti_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   std::vector<std::pair<bs::Address, std::string>> result;
   result.reserve(response.addresses_size());
   for (const auto &addr : response.addresses()) {
      try {
         const auto &address = bs::Address::fromAddressString(addr.address());
         result.push_back({ address, addr.index() });
      }
      catch (const std::exception &) {}
   }
   itReqMulti->second(result);
   reqSyncNewAddrMulti_.erase(itReqMulti);
   return true;
}

bool SignerClient::processWalletSync(uint64_t msgId, const SignerMessage_WalletData &response)
{
   const auto &itReq = reqSyncWalletMap_.find(msgId);
   if (itReq == reqSyncWalletMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   bs::sync::WalletData wd;
   wd.highestExtIndex = response.high_ext_index();
   wd.highestIntIndex = response.high_int_index();
   wd.addresses.reserve(response.addresses_size());
   wd.addrPool.reserve(response.addr_pool_size());
   wd.txComments.reserve(response.tx_comments_size());

   for (const auto &addr : response.addresses()) {
      try {
         const auto &address = bs::Address::fromAddressString(addr.address());
         wd.addresses.push_back({addr.index(), address, addr.comment()});
      }
      catch (const std::exception &) {}
   }
   for (const auto &addr : response.addr_pool()) {
      try {
         const auto &address = bs::Address::fromAddressString(addr.address());
         wd.addrPool.push_back({ addr.index(), address, addr.comment() });
      } catch (const std::exception &) {}
   }
   for (const auto &txCom : response.tx_comments()) {
      wd.txComments.push_back({BinaryData::fromString(txCom.tx_hash())
         , txCom.comment()});
   }
   itReq->second(wd);
   reqSyncWalletMap_.erase(itReq);
   return true;
}

bool SignerClient::processHdWalletSync(uint64_t msgId, const HDWalletData &response)
{
   const auto &itReq = reqSyncHdWalletMap_.find(msgId);
   if (itReq == reqSyncHdWalletMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   const auto &wd = bs::sync::HDWalletData::fromCommonMessage(response);
   itReq->second(wd);
   reqSyncHdWalletMap_.erase(itReq);
   return true;
}

bool SignerClient::processSetSettlId(uint64_t msgId, bool result)
{
   const auto &itReq = reqSettlIdMap_.find(msgId);
   if (itReq == reqSettlIdMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   itReq->second(result);
   reqSettlIdMap_.erase(itReq);
   return true;
}

bool SignerClient::processRootPubKey(uint64_t msgId
   , const SignerMessage_RootPubKey &response)
{
   const auto &itReq = reqPubKeyMap_.find(msgId);
   if (itReq == reqPubKeyMap_.end()) {
      logger_->warn("[{}] no mapping for msg #{}", __func__, msgId);
      return false;
   }
   itReq->second(response.success(), BinaryData::fromString(response.pub_key()));
   reqPubKeyMap_.erase(itReq);
   return true;
}

void SignerClient::syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &cb)
{
   SignerMessage msg;
   msg.mutable_start_wallets_sync();
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncWalletInfoMap_[env.id] = cb;
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

bs::signer::RequestId SignerClient::setUserId(const BinaryData& userId
   , const std::string& walletId)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_set_user_id();
   msgReq->set_user_id(userId.toBinStr());
   msgReq->set_wallet_id(walletId);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   return (bs::signer::RequestId)env.id;
}

void SignerClient::syncNewAddress(const std::string &walletId, const std::string &index
   , const std::function<void(const bs::Address &)> &cb)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_sync_new_addresses();
   msgReq->set_wallet_id(walletId);
   msgReq->add_indices(index);
   msgReq->set_single(true);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncNewAddrSingle_[env.id] = cb;
}

void SignerClient::syncNewAddresses(const std::string &walletId, const std::vector<std::string> &indices
   , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &cb)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_sync_new_addresses();
   msgReq->set_wallet_id(walletId);
   for (const auto &index : indices) {
      msgReq->add_indices(index);
   }
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncNewAddrMulti_[env.id] = cb;
}

void SignerClient::syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &cb)
{
   SignerMessage msg;
   msg.set_sync_wallet(id);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncWalletMap_[env.id] = cb;
}

void SignerClient::syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &cb)
{
   SignerMessage msg;
   msg.set_sync_hd_wallet(id);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncHdWalletMap_[env.id] = cb;
}

void SignerClient::syncAddressComment(const std::string &walletId
   , const bs::Address &addr, const std::string &comment)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_sync_addr_comment();
   msgReq->set_wallet_id(walletId);
   msgReq->set_address(addr.display());
   msgReq->set_comment(comment);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
}

void SignerClient::syncTxComment(const std::string &walletId
   , const BinaryData &txHash, const std::string &comment)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_sync_tx_comment();
   msgReq->set_wallet_id(walletId);
   msgReq->set_tx_hash(txHash.toBinStr());
   msgReq->set_comment(comment);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
}

void SignerClient::extendAddressChain(const std::string &walletId, unsigned count, bool extInt,
   const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &cb)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_ext_addr_chain();
   msgReq->set_wallet_id(walletId);
   msgReq->set_count(count);
   msgReq->set_ext_int(extInt);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSyncNewAddrMulti_[env.id] = cb;
}

void SignerClient::setSettlementID(const std::string &walletId, const SecureBinaryData &id
   , const std::function<void(bool)> &cb)
{
   SignerMessage msg;
   auto msgReq = msg.mutable_set_settl_id();
   msgReq->set_wallet_id(walletId);
   msgReq->set_settlement_id(id.toBinStr());
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqSettlIdMap_[env.id] = cb;
}

void SignerClient::getRootPubkey(const std::string &walletID
   , const std::function<void(bool, const SecureBinaryData &)> &cb)
{
   SignerMessage msg;
   msg.set_get_root_pubkey(walletID);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   reqPubKeyMap_[env.id] = cb;
}

bs::signer::RequestId SignerClient::DeleteHDRoot(const std::string &rootWalletId)
{
   SignerMessage msg;
   msg.set_del_hd_root(rootWalletId);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   return (unsigned int)env.id;
}

bs::signer::RequestId SignerClient::DeleteHDLeaf(const std::string &leafWalletId)
{
   SignerMessage msg;
   msg.set_del_hd_leaf(leafWalletId);
   Envelope env{ 0, clientUser_, signerUser_, {}, {}, msg.SerializeAsString(), true };
   queue_->pushFill(env);
   return (unsigned int)env.id;
}
