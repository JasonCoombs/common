/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WalletsAdapter.h"
#include <spdlog/spdlog.h>
#include "SignerClient.h"
#include "Wallets/SyncHDWallet.h"

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;
using namespace bs::sync;


WalletsAdapter::WalletsAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &ownUser
   , std::unique_ptr<SignerClient> signerClient
   , const std::shared_ptr<bs::message::User> &blockchainUser)
   : logger_(logger), ownUser_(ownUser), signerClient_(std::move(signerClient))
   , blockchainUser_(blockchainUser)
{
   signerClient_->setClientUser(ownUser_);
   signerClient_->setSignerReady([this] {
      sendLoadingBC();
   });
   signerClient_->setWalletsLoaded([this] {
      startWalletsSync();
   });
   signerClient_->setNoWalletsFound([this] {
      logger_->debug("[WalletsAdapter] no wallets found");
   });
   signerClient_->setWalletsListUpdated([this] { reset(); });
}

bool WalletsAdapter::process(const Envelope &env)
{
   if (signerClient_->isSignerUser(env.sender)) {
      return signerClient_->process(env);
   }
   else if (env.sender->value() == blockchainUser_->value()) {
      return processBlockchain(env);
   }
   else if (env.receiver && (env.receiver->value() == ownUser_->value())) {
      return processOwnRequest(env);
   }
   return true;
}

bool WalletsAdapter::processBlockchain(const Envelope &env)
{
   if (!env.receiver && env.request) {
      return true;
   }
   ArmoryMessage msg;
   if (!msg.ParseFromString(env.message)) {
      logger_->error("[{}] failed to parse msg #{}", __func__, env.id);
      return true;
   }
   switch (msg.data_case()) {
   case ArmoryMessage::kZcReceived:
      processZCReceived(msg.zc_received());
      break;
   case ArmoryMessage::kWalletRegistered:
      if (msg.wallet_registered().success() && !msg.wallet_registered().wallet_id().empty()) {
         processWalletRegistered(msg.wallet_registered().wallet_id());
      }
      else {
         sendWalletError(msg.wallet_registered().wallet_id(), "registration failed");
      }
      break;
   case ArmoryMessage::kUnconfTargetSet:
      processUnconfTgtSet(msg.unconf_target_set());
      break;
   case ArmoryMessage::kAddrTxnResponse:
      processAddrTxN(msg.addr_txn_response());
      break;
   case ArmoryMessage::kWalletBalanceResponse:
      processWalletBal(msg.wallet_balance_response());
      break;
   case ArmoryMessage::kTransactions:
      processTransactions(env.id, msg.transactions());
      break;
   default: break;
   }
   return true;
}

void WalletsAdapter::sendLoadingBC()
{
   WalletsMessage msg;
   msg.mutable_loading();
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::startWalletsSync()
{
   const auto &cbWalletsReceived = [this](const std::vector<bs::sync::WalletInfo> &wallets)
   {
      for (const auto &wallet : wallets) {
         loadingWallets_.insert(wallet.id);
      }
      for (const auto &wallet : wallets) {
         loadWallet(wallet);
      }
   };
   signerClient_->syncWalletInfo(cbWalletsReceived);
}

void WalletsAdapter::loadWallet(const bs::sync::WalletInfo &info)
{
   logger_->debug("[WalletsManager::syncWallets] syncing wallet {} ({} {})"
      , info.id, info.name, info.description);

   switch (info.format) {
   case bs::sync::WalletFormat::HD:
   {
      try {
         const auto hdWallet = std::make_shared<hd::Wallet>(info, signerClient_.get(), logger_);
         hdWallet->setWCT(this);

         if (hdWallet) {
            const auto &cbHDWalletDone = [this, hdWallet]
            {
               logger_->debug("[WalletsAdapter::loadWallet] synced HD wallet {}"
                  , hdWallet->walletId());
               saveWallet(hdWallet);

               const auto &wi = bs::sync::WalletInfo::fromWallet(hdWallet);
               WalletsMessage msg;
               auto msgWallet = msg.mutable_wallet_loaded();
               wi.toCommonMsg(*msgWallet);
               Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
               pushFill(env);

               loadingWallets_.erase(hdWallet->walletId());
               if (loadingWallets_.empty()) {
                  ArmoryMessage msg;
                  auto msgReq = msg.mutable_register_wallet();
                  msgReq->set_wallet_id("");
                  Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
                  pushFill(env);
               }
            };
            hdWallet->synchronize(cbHDWalletDone);
         }
      } catch (const std::exception &e) {
         logger_->error("[WalletsAdapter::loadWallet] failed to create HD wallet "
            "{}: {}", info.id, e.what());
      }
      break;
   }

   case bs::sync::WalletFormat::Settlement:
      throw std::runtime_error("not supported");
      break;

   default:
      logger_->warn("[WalletsAdapter::loadWallet] wallet format {} is not "
         "supported yet", (int)info.format);
      break;
   }
}

std::shared_ptr<hd::Wallet> WalletsAdapter::getHDWalletById(const std::string& walletId) const
{
   auto it = std::find_if(hdWallets_.cbegin(), hdWallets_.cend()
      , [walletId](const std::shared_ptr<hd::Wallet> &hdWallet) {
      return (hdWallet->walletId() == walletId);
   });
   if (it != hdWallets_.end()) {
      return *it;
   }
   return nullptr;
}

std::shared_ptr<Wallet> WalletsAdapter::getWalletById(const std::string &walletId) const
{
   for (const auto &wallet : wallets_) {
      if (wallet.second->hasId(walletId)) {
         return wallet.second;
      }
   }
   return nullptr;
}

std::shared_ptr<Wallet> WalletsAdapter::getWalletByAddress(const bs::Address &address) const
{
   for (const auto &wallet : wallets_) {
      if (wallet.second && (wallet.second->containsAddress(address)
         || wallet.second->containsHiddenAddress(address))) {
         return wallet.second;
      }
   }
   return nullptr;
}

std::shared_ptr<hd::Group> WalletsAdapter::getGroupByWalletId(const std::string &walletId) const
{
   const auto itGroup = groupsByWalletId_.find(walletId);
   if (itGroup == groupsByWalletId_.end()) {
      const auto hdWallet = getHDRootForLeaf(walletId);
      if (hdWallet) {
         for (const auto &group : hdWallet->getGroups()) {
            for (const auto &leaf : group->getLeaves()) {
               if (leaf->hasId(walletId)) {
                  groupsByWalletId_[walletId] = group;
                  return group;
               }
            }
         }
      }
      groupsByWalletId_[walletId] = nullptr;
      return nullptr;
   }
   return itGroup->second;
}

std::shared_ptr<hd::Wallet> WalletsAdapter::getHDRootForLeaf(const std::string &walletId) const
{
   for (const auto &hdWallet : hdWallets_) {
      for (const auto &leaf : hdWallet->getLeaves()) {
         if (leaf->hasId(walletId)) {
            return hdWallet;
         }
      }
   }
   return nullptr;
}

void WalletsAdapter::eraseWallet(const std::shared_ptr<Wallet> &wallet)
{
   if (!wallet) {
      return;
   }
   wallets_.erase(wallet->walletId());
}

void WalletsAdapter::saveWallet(const std::shared_ptr<bs::sync::hd::Wallet> &wallet)
{
   if (!userId_.empty()) {
      wallet->setUserId(userId_);
   }
   const auto existingHdWallet = getHDWalletById(wallet->walletId());

   if (existingHdWallet) {    // merge if HD wallet already exists
      existingHdWallet->merge(*wallet);
   } else {
      hdWallets_.push_back(wallet);
   }

   for (const auto &leaf : wallet->getLeaves()) {
      addWallet(leaf);
   }
}

void WalletsAdapter::addWallet(const std::shared_ptr<Wallet> &wallet)
{
   auto ccLeaf = std::dynamic_pointer_cast<bs::sync::hd::CCLeaf>(wallet);
   if (ccLeaf) {
//      ccLeaf->setCCDataResolver(ccResolver_);
//      updateTracker(ccLeaf);   //TODO: should be request to OnChainTrackerClient
   }
   wallet->setUserId(userId_);

   const auto &itWallet = wallets_.find(wallet->walletId());
   if (itWallet != wallets_.end()) {
      itWallet->second->merge(wallet);
   } else {
      wallets_[wallet->walletId()] = wallet;
   }

   if ((wallet->type() == bs::core::wallet::Type::Authentication)) {
      authAddressWallet_ = wallet;
      logger_->debug("[WalletsAdapter::addWallet] auth leaf {} created", wallet->walletId());
      WalletsMessage msg;
      msg.set_auth_leaf_created(authAddressWallet_->walletId());
      Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
      pushFill(env);
   }
   registerWallet(wallet);
}

void WalletsAdapter::registerWallet(const std::shared_ptr<Wallet> &wallet)
{
   const auto &regData = wallet->regData();
   for (const auto &reg : regData) {
      ArmoryMessage msg;
      auto msgReq = msg.mutable_register_wallet();
      msgReq->set_wallet_id(reg.first);
      msgReq->set_as_new(false);
      for (const auto &addr : reg.second) {
         msgReq->add_addresses(addr.toBinStr());
      }
      Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      pushFill(env);
   }
}

void WalletsAdapter::registerWallet(const std::shared_ptr<bs::sync::hd::Wallet> &hdWallet)
{
   for (const auto &leaf : hdWallet->getLeaves()) {
      //settlement leaves are not registered
      if (leaf->type() == bs::core::wallet::Type::Settlement) {
         continue;
      }
      registerWallet(leaf);
   }
}

void WalletsAdapter::reset()
{
   userId_.clear();
   hdWallets_.clear();
   wallets_.clear();
   walletNames_.clear();
   readyWallets_.clear();
   authAddressWallet_.reset();
   startWalletsSync();
}

void WalletsAdapter::balanceUpdated(const std::string &walletId)
{
   WalletsMessage msg;
   msg.set_balance_updated(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::sendWalletChanged(const std::string &walletId)
{
   WalletsMessage msg;
   msg.set_wallet_changed(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::sendWalletReady(const std::string &walletId)
{
   readyWallets_.insert(walletId);
   WalletsMessage msg;
   msg.set_wallet_ready(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::sendWalletError(const std::string &walletId
   , const std::string &errMsg)
{
   WalletsMessage msg;
   auto msgError = msg.mutable_error();
   msgError->set_wallet_id(walletId);
   msgError->set_error_message(errMsg);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::addressAdded(const std::string &walletId)
{
   sendWalletChanged(walletId);
}

void WalletsAdapter::metadataChanged(const std::string &walletId)
{
   WalletsMessage msg;
   msg.set_wallet_meta_changed(walletId);
   Envelope env{ 0, ownUser_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void WalletsAdapter::processWalletRegistered(const std::string &walletId)
{
   for (const auto &wallet : wallets_) {
      if (wallet.second->hasId(walletId)) {
         wallet.second->onRegistered();

         const auto &unconfTgts = wallet.second->unconfTargets();
         const auto &itUnconfTgt = unconfTgts.find(walletId);
         if (!balanceEnabled() || (itUnconfTgt == unconfTgts.end())) {
            sendWalletReady(wallet.second->walletId());
         }
         else {
            ArmoryMessage msg;
            auto msgUnconfTgt = msg.mutable_set_unconf_target();
            msgUnconfTgt->set_wallet_id(walletId);
            msgUnconfTgt->set_conf_count(itUnconfTgt->second);
            Envelope env{ 0, ownUser_, blockchainUser_, {}, {}
               , msg.SerializeAsString(), true };
            pushFill(env);
         }
         break;
      }
   }
}

void WalletsAdapter::processUnconfTgtSet(const std::string &walletId)
{
   sendTxNRequest(walletId);
   sendBalanceRequest(walletId);
}

void WalletsAdapter::processAddrTxN(const ArmoryMessage_AddressTxNsResponse &response)
{
   for (const auto &byWallet : response.wallet_txns()) {
      auto &balanceData = walletBalances_[byWallet.wallet_id()];
      balanceData.addrTxNUpdated = true;
      for (const auto &txn : byWallet.txns()) {
         balanceData.addressTxNMap[BinaryData::fromString(txn.address())] = txn.txn();
      }

      if (trackLiveAddresses()) {
         sendTrackAddrRequest(byWallet.wallet_id());
      }
      else {
         if (balanceData.addrBalanceUpdated) {
            sendWalletReady(byWallet.wallet_id());
         }
      }
   }
}

void WalletsAdapter::processWalletBal(const ArmoryMessage_WalletBalanceResponse &response)
{
   for (const auto &byWallet : response.balances()) {
      auto &balanceData = walletBalances_[byWallet.wallet_id()];
      balanceData.walletBalance.totalBalance = byWallet.full_balance();
      balanceData.walletBalance.unconfirmedBalance = byWallet.unconfirmed_balance();
//      balanceData.walletBalance.spendableBalance = byWallet.spendable_balance();
      balanceData.walletBalance.spendableBalance = balanceData.walletBalance.totalBalance - balanceData.walletBalance.unconfirmedBalance;
      balanceData.addrCount = byWallet.address_count();
      balanceData.addrBalanceUpdated = true;
      for (const auto &addrBal : byWallet.addr_balances()) {
         auto &addrBalance = balanceData.addressBalanceMap[BinaryData::fromString(addrBal.address())];
         addrBalance.totalBalance = addrBal.full_balance();
         addrBalance.spendableBalance = addrBal.spendable_balance();
         addrBalance.unconfirmedBalance = addrBal.unconfirmed_balance();
      }

      if (trackLiveAddresses()) {
         sendTrackAddrRequest(byWallet.wallet_id());
      }
      else {
         if (balanceData.addrTxNUpdated) {
            sendWalletReady(byWallet.wallet_id());
         }
      }
   }
}

void WalletsAdapter::sendTrackAddrRequest(const std::string &walletId)
{
   const auto &cb = [this, walletId](bs::sync::SyncState st)
   {
      if (st == bs::sync::SyncState::Success) {
         const auto &wallet = getWalletById(walletId);
         if (wallet) {
            const auto &cbSync = [this, walletId]
            {
               sendBalanceRequest(walletId);
               //TODO: top up if needed
               sendWalletReady(walletId);
            };
            wallet->synchronize(cbSync);
         }
      }
      else {
         sendWalletReady(walletId);
      }
   };

   const auto &balanceData = walletBalances_[walletId];
   if (!balanceData.addrTxNUpdated || !balanceData.addrBalanceUpdated) {
      return;  // wait for both requests to complete first
   }
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] can't find wallet for {}", __func__, walletId);
      return;
   }
   std::set<BinaryData> usedAddrSet;
   for (const auto &addrPair : balanceData.addressTxNMap) {
      if (addrPair.second != 0) {
         usedAddrSet.insert(addrPair.first);
      }
   }
   for (const auto &addrPair : balanceData.addressBalanceMap) {
      if (usedAddrSet.find(addrPair.first) != usedAddrSet.end()) {
         continue;   // skip already added addresses
      }
      if (addrPair.second.totalBalance) {
         usedAddrSet.insert(addrPair.first);
      }
   }

   std::set<BinaryData> usedAndRegAddrs;
   const auto &regAddresses = wallet->allAddresses();
   std::set_intersection(regAddresses.cbegin(), regAddresses.cend()
      , usedAddrSet.cbegin(), usedAddrSet.cend()
      , std::inserter(usedAndRegAddrs, usedAndRegAddrs.end()));
   signerClient_->syncAddressBatch(walletId, usedAndRegAddrs, cb);
}

void WalletsAdapter::sendTxNRequest(const std::string &walletId)
{
   walletBalances_[walletId].addrTxNUpdated = false;
   ArmoryMessage msg;
   auto msgReq = msg.mutable_addr_txn_request();
   msgReq->add_wallet_ids(walletId);
   Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   pushFill(env);
}

void WalletsAdapter::sendBalanceRequest(const std::string &walletId)
{
   walletBalances_[walletId].addrTxNUpdated = false;
   ArmoryMessage msg;
   auto msgReq = msg.mutable_wallet_balance_request();
   msgReq->add_wallet_ids(walletId);
   Envelope env{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   pushFill(env);
}

void WalletsAdapter::scanComplete(const std::string &walletId)
{
   logger_->debug("[{}] - HD wallet {} imported", __func__, walletId);
   const auto hdWallet = getHDWalletById(walletId);
   if (hdWallet) {
      registerWallet(hdWallet);
   }
   sendWalletChanged(walletId);
//   emit walletImportFinished(walletId);
}

void WalletsAdapter::walletReset(const std::string &walletId)
{
   sendWalletChanged(walletId);
}

void WalletsAdapter::walletCreated(const std::string &walletId)
{
   for (const auto &hdWallet : hdWallets_) {
      const auto leaf = hdWallet->getLeaf(walletId);
      if (leaf == nullptr) {
         continue;
      }
      logger_->debug("[WalletsAdapter::walletCreated] HD leaf {} ({}) added"
         , walletId, leaf->name());

      addWallet(leaf);
      sendWalletChanged(walletId);
      break;
   }
}

void WalletsAdapter::walletDestroyed(const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   eraseWallet(wallet);
   sendWalletChanged(walletId);
}

void WalletsAdapter::processZCReceived(const ArmoryMessage_ZCReceived &event)
{
   //TODO: update TX counts for participating addresses

   std::unordered_set<std::string> participatingWalletIds;
   for (const auto &entry : event.tx_entries()) {
      for (const auto &walletId : entry.wallet_ids()) {
         participatingWalletIds.insert(walletId);
      }
   }
   for (const auto &walletId : participatingWalletIds) {
      sendBalanceRequest(walletId);
   }
}

bool WalletsAdapter::processOwnRequest(const bs::message::Envelope &env)
{
   WalletsMessage msg;
   if (!msg.ParseFromString(env.message)) {
      logger_->error("[{}] failed to parse msg #{}", __func__, env.id);
      return true;
   }
   switch (msg.data_case()) {
   case WalletsMessage::kHdWalletGet:
      return processHdWalletGet(env, msg.hd_wallet_get());
   case WalletsMessage::kWalletGet:
      return processWalletGet(env, msg.wallet_get());
   case WalletsMessage::kTxCommentGet:
      return processGetTxComment(env, msg.tx_comment_get());
   case WalletsMessage::kGetWalletBalances:
      return processGetWalletBalances(env, msg.get_wallet_balances());
   case WalletsMessage::kGetExtAddresses:
      return processGetExtAddresses(env, msg.get_ext_addresses());
   case WalletsMessage::kGetIntAddresses:
      return processGetIntAddresses(env, msg.get_int_addresses());
   case WalletsMessage::kGetUsedAddresses:
      return processGetUsedAddresses(env, msg.get_used_addresses());
   case WalletsMessage::kGetAddrComments:
      return processGetAddrComments(env, msg.get_addr_comments());
   case WalletsMessage::kSetAddrComments:
      return processSetAddrComments(env, msg.set_addr_comments());
   case WalletsMessage::kTxDetailsRequest:
      return processTXDetails(env, msg.tx_details_request());
   default: break;
   }
   return true;
}

bool WalletsAdapter::processHdWalletGet(const Envelope &env
   , const std::string &walletId)
{
   const auto &hdWallet = getHDWalletById(walletId);
   if (!hdWallet) {
      logger_->error("[{}] HD wallet {} not found", __func__, walletId);
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_hd_wallet();
   msgResp->set_wallet_id(hdWallet->walletId());
   msgResp->set_is_primary(hdWallet->isPrimary());
   for (const auto &group : hdWallet->getGroups()) {
      auto msgGroup = msgResp->add_groups();
      msgGroup->set_type(group->index());
      msgGroup->set_ext_only(group->extOnly());
      msgGroup->set_name(group->name());
      msgGroup->set_desc(group->description());

      const auto &authGroup = std::dynamic_pointer_cast<bs::sync::hd::AuthGroup>(group);
      if (authGroup && !authGroup->userId().empty()) {
         msgGroup->set_salt(authGroup->userId().toBinStr());
      }

      for (const auto &leaf : group->getLeaves()) {
         auto msgLeaf = msgGroup->add_leaves();
         msgLeaf->set_id(leaf->walletId());
         msgLeaf->set_path(leaf->path().toString());
         msgLeaf->set_name(leaf->shortName());
         msgLeaf->set_desc(leaf->description());
         msgLeaf->set_ext_only(leaf->extOnly());
      }
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processWalletGet(const Envelope &env
   , const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_wallet_data();
   msgResp->set_wallet_id(wallet->walletId());
   for (const auto &addr : wallet->getUsedAddressList()) {
      auto msgAddr = msgResp->add_used_addresses();
      const auto &idx = wallet->getAddressIndex(addr);
      const auto &comment = wallet->getAddressComment(addr);
      msgAddr->set_index(idx);
      msgAddr->set_address(addr.display());
      msgAddr->set_comment(comment);
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processGetTxComment(const Envelope &env
   , const std::string &txBinHash)
{
   const auto &txHash = BinaryData::fromString(txBinHash);
   for (const auto &wallet : wallets_) {
      const auto &comment = wallet.second->getTransactionComment(txHash);
      if (!comment.empty()) {
         WalletsMessage msg;
         auto msgResp = msg.mutable_tx_comment();
         msgResp->set_tx_hash(txBinHash);
         msgResp->set_comment(comment);
         Envelope envResp{ env.id, ownUser_, env.sender, {}, {}
            , msg.SerializeAsString() };
         return pushFill(envResp);
      }
   }
   return true;
}

bool WalletsAdapter::processGetWalletBalances(const bs::message::Envelope &env
   , const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   if (readyWallets_.find(walletId) == readyWallets_.end()) {
      return false;  // postpone processing until wallet will become ready
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_wallet_balances();
   msgResp->set_wallet_id(walletId);
   double totalBalance = 0, spendableBalance = 0, unconfirmedBalance = 0;
   unsigned int addrCount = 0;
   for (const auto &id : wallet->internalIds()) {
      const auto &itBal = walletBalances_.find(id);
      if (itBal == walletBalances_.end()) {
         continue;
      }
      totalBalance += itBal->second.walletBalance.totalBalance / BTCNumericTypes::BalanceDivider;
      spendableBalance += itBal->second.walletBalance.spendableBalance / BTCNumericTypes::BalanceDivider;
      unconfirmedBalance += itBal->second.walletBalance.unconfirmedBalance / BTCNumericTypes::BalanceDivider;
      addrCount += itBal->second.addrCount;
      for (const auto &addrTxN : itBal->second.addressTxNMap) {
         auto msgAddr = msgResp->add_address_balances();
         msgAddr->set_address(addrTxN.first.toBinStr());
         msgAddr->set_txn(addrTxN.second);
         const auto &itAddrBal = itBal->second.addressBalanceMap.find(addrTxN.first);
         if (itAddrBal != itBal->second.addressBalanceMap.end()) {
            msgAddr->set_total_balance(itAddrBal->second.totalBalance);
            msgAddr->set_spendable_balance(itAddrBal->second.spendableBalance);
            msgAddr->set_unconfirmed_balance(itAddrBal->second.unconfirmedBalance);
         }
      }
   }
   msgResp->set_total_balance(totalBalance);
   msgResp->set_spendable_balance(spendableBalance);
   msgResp->set_unconfirmed_balance(unconfirmedBalance);
   msgResp->set_nb_addresses(addrCount);
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processGetExtAddresses(const bs::message::Envelope &env, const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   return sendAddresses(env, walletId, wallet->getExtAddressList());
}

bool WalletsAdapter::processGetIntAddresses(const bs::message::Envelope &env, const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   return sendAddresses(env, walletId, wallet->getIntAddressList());
}

bool WalletsAdapter::processGetUsedAddresses(const bs::message::Envelope &env, const std::string &walletId)
{
   const auto &wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, walletId);
      return true;
   }
   return sendAddresses(env, walletId, wallet->getUsedAddressList());
}

bool WalletsAdapter::sendAddresses(const bs::message::Envelope &env, const std::string &walletId
   , const std::vector<bs::Address> &addrs)
{
   WalletsMessage msg;
   auto msgResp = msg.mutable_wallet_addresses();
   msgResp->set_wallet_id(walletId);
   for (const auto &addr : addrs) {
      msgResp->add_addresses(addr.display());
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processGetAddrComments(const bs::message::Envelope &env
   , const BlockSettle::Common::WalletsMessage_WalletAddresses &request)
{
   const auto &wallet = getWalletById(request.wallet_id());
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, request.wallet_id());
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_addr_comments();
   msgResp->set_wallet_id(wallet->walletId());
   for (const auto &addrStr : request.addresses()) {
      try {
         const auto &addr = bs::Address::fromAddressString(addrStr);
         const auto &comment = wallet->getAddressComment(addr);
         if (!comment.empty()) {
            auto commentData = msgResp->add_comments();
            commentData->set_address(addrStr);
            commentData->set_comment(comment);
         }
      }
      catch (const std::exception &) {}
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processSetAddrComments(const bs::message::Envelope &env
   , const BlockSettle::Common::WalletsMessage_AddressComments &request)
{
   const auto &wallet = getWalletById(request.wallet_id());
   if (!wallet) {
      logger_->error("[{}] wallet {} not found", __func__, request.wallet_id());
      return true;
   }
   WalletsMessage msg;
   auto msgResp = msg.mutable_addr_comments();
   msgResp->set_wallet_id(request.wallet_id());
   for (const auto &comm : request.comments()) {
      try {
         const auto &addr = bs::Address::fromAddressString(comm.address());
         if (wallet->setAddressComment(addr, comm.comment())) {
            auto commData = msgResp->add_comments();
            commData->set_address(comm.address());
            commData->set_comment(comm.comment());
         }
      }
      catch (const std::exception &) {}
   }
   Envelope envResp{ env.id, ownUser_, env.sender, {}, {}, msg.SerializeAsString() };
   return pushFill(envResp);
}

bool WalletsAdapter::processTXDetails(const bs::message::Envelope &env
   , const BlockSettle::Common::WalletsMessage_TXDetailsRequest &request)
{
   std::set<BinaryData> initialHashes;
   std::vector<bs::sync::TXWallet>  requests;
   for (const auto &req : request.requests()) {
      const auto &txHash = BinaryData::fromString(req.tx_hash());
      requests.push_back({ txHash, req.wallet_id(), req.value() });
      initialHashes.insert(txHash);
   }
   ArmoryMessage msg;
   auto msgReq = msg.mutable_get_txs_by_hash();
   for (const auto &txHash : initialHashes) {
      msgReq->add_tx_hashes(txHash.toBinStr());
   }
   Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
   if (pushFill(envReq)) {
      initialHashes_[envReq.id] = { env, std::map<BinaryData, Tx>{}, requests };
      return true;
   }
   return false;
}

void WalletsAdapter::processTransactions(uint64_t msgId
   , const ArmoryMessage_Transactions &response)
{
   const auto &convertTXs = [response]() -> std::vector<Tx>
   {
      std::vector<Tx> result;
      for (const auto &txSer : response.transactions()) {
         const Tx tx(BinaryData::fromString(txSer));
         result.emplace_back(std::move(tx));
      }
      return result;
   };

   auto itId = initialHashes_.find(msgId);
   if (itId != initialHashes_.end()) {
      auto data = itId->second;
      initialHashes_.erase(itId);
      const auto &initialTXs = convertTXs();
      for (const auto &tx : initialTXs) {
         data.allTXs[tx.getThisHash()] = tx;
      }
      std::set<BinaryData> prevHashes;
      for (const auto &tx : initialTXs) {
         for (size_t i = 0; i < tx.getNumTxIn(); i++) {
            TxIn in = tx.getTxInCopy(i);
            OutPoint op = in.getOutPoint();
            if (data.allTXs.find(op.getTxHash()) == data.allTXs.end()) {
               prevHashes.insert(op.getTxHash());
            }
         }
      }
      ArmoryMessage msg;
      auto msgReq = msg.mutable_get_txs_by_hash();
      for (const auto &txHash : prevHashes) {
         msgReq->add_tx_hashes(txHash.toBinStr());
      }
      Envelope envReq{ 0, ownUser_, blockchainUser_, {}, {}, msg.SerializeAsString(), true };
      if (pushFill(envReq)) {
         prevHashes_[envReq.id] = data;
      }
      return;
   }

   itId = prevHashes_.find(msgId);
   if (itId != prevHashes_.end()) {
      for (const auto &tx : convertTXs()) {
         itId->second.allTXs[tx.getThisHash()] = tx;
      }
      
      WalletsMessage msg;
      auto msgResp = msg.mutable_tx_details_response();
      for (const auto &req : itId->second.requests) {
         auto resp = msgResp->add_responses();
         resp->set_tx_hash(req.txHash.toBinStr());
         resp->set_wallet_id(req.walletId);
         const auto &wallet = getWalletById(req.walletId);
         if (wallet) {
            resp->set_wallet_name(wallet->name());
            resp->set_wallet_type((int)wallet->type());
            resp->set_comment(wallet->getTransactionComment(req.txHash));
            resp->set_valid(wallet->isTxValid(req.txHash) == bs::sync::TxValidity::Valid);
            resp->set_amount(wallet->displayTxValue(req.value).toStdString());
            resp->set_direction((int)getDirection(req.txHash, wallet
               , itId->second.allTXs));

            const auto &itTx = itId->second.allTXs.find(req.txHash);
            if (itTx != itId->second.allTXs.end()) {
               const bool isReceiving = (req.value > 0);
               std::set<bs::Address> ownAddresses, foreignAddresses;
               for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
                  const TxOut &out = itTx->second.getTxOutCopy((int)i);
                  try {
                     const auto &addr = bs::Address::fromTxOut(out);
                     const auto &addrWallet = getWalletByAddress(addr);
                     if (addrWallet == wallet) {
                        ownAddresses.insert(addr);
                     } else {
                        foreignAddresses.insert(addr);
                     }
                  } catch (const std::exception &) {
                     // address conversion failure - likely OP_RETURN - do nothing
                  }
               }
               if (!isReceiving && (ownAddresses.size() == 1) && !foreignAddresses.empty()) {
                  if (!wallet->isExternalAddress(*ownAddresses.cbegin())) {
                     ownAddresses.clear();   // treat the only own internal address as change and throw away
                  }
               }
               const auto &setAddresses = [&resp](const std::set<bs::Address> &addrs)
               {
                  for (const auto &addr : addrs) {
                     resp->add_out_addresses(addr.display());
                  }
               };
               if (!ownAddresses.empty()) {
                  setAddresses(ownAddresses);
               } else {
                  setAddresses(foreignAddresses);
               }
            }
         }
      }
      Envelope envResp{ itId->second.env.id, ownUser_, itId->second.env.sender
         , {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
      prevHashes_.erase(itId);
   }
}

Transaction::Direction WalletsAdapter::getDirection(const BinaryData &txHash
   , const std::shared_ptr<Wallet> &wallet, const std::map<BinaryData, Tx> &allTXs) const
{
   const auto &itTx = allTXs.find(txHash);
   if (itTx == allTXs.end()) {
      return Transaction::Direction::Unknown;
   }
   if (wallet->type() == bs::core::wallet::Type::Authentication) {
      return Transaction::Auth;
   } else if (wallet->type() == bs::core::wallet::Type::ColorCoin) {
      return Transaction::Delivery;
   }
   const auto &group = getGroupByWalletId(wallet->walletId());
   bool ourOuts = false;
   bool otherOuts = false;
   bool ourIns = false;
   bool otherIns = false;
   bool ccTx = false;

   for (size_t i = 0; i < itTx->second.getNumTxIn(); ++i) {
      const TxIn &in = itTx->second.getTxInCopy((int)i);
      const OutPoint &op = in.getOutPoint();
      const auto &itPrevTx = allTXs.find(op.getTxHash());
      if (itPrevTx == allTXs.end()) {
         continue;
      }
      const auto &prevOut = itPrevTx->second.getTxOutCopy(op.getTxOutIndex());
      const auto &addr = bs::Address::fromTxOut(prevOut);
      const auto &addrWallet = getWalletByAddress(addr);
      const auto &addrGroup = addrWallet ? getGroupByWalletId(addrWallet->walletId())
         : nullptr;
      if ((addrWallet && (addrWallet == wallet)) || (group && (group == addrGroup))) {
         ourIns = true;
      }
      else {
         otherIns = true;
      }
      if (addrWallet && (addrWallet->type() == bs::core::wallet::Type::ColorCoin)) {
         ccTx = true;
      }
   }
   for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
      const TxOut &out = itTx->second.getTxOutCopy((int)i);
      const auto &addr = bs::Address::fromTxOut(out);
      const auto &addrWallet = getWalletByAddress(addr);
      const auto &addrGroup = addrWallet ? getGroupByWalletId(addrWallet->walletId())
         : nullptr;
      if ((addrWallet && (addrWallet == wallet)) || (group && (group == addrGroup))) {
         ourOuts = true;
      }
      else {
         otherOuts = true;
      }
      if (addrWallet && (addrWallet->type() == bs::core::wallet::Type::ColorCoin)) {
         ccTx = true;
         break;
      }
      else if (!ourOuts) {
         if ((group && addrGroup) && (group == addrGroup)) {
            ourOuts = true;
            otherOuts = false;
         }
      }
   }
   if (wallet->type() == bs::core::wallet::Type::Settlement) {
      if (ourOuts) {
         return Transaction::PayIn;
      }
      return Transaction::PayOut;
   }
   if (ccTx) {
      return Transaction::Payment;
   }
   if (ourOuts && ourIns && !otherOuts && !otherIns) {
      return Transaction::Internal;
   }
   if (!ourIns) {
      return Transaction::Received;
   }
   if (otherOuts) {
      return Transaction::Sent;
   }
   return Transaction::Direction::Unknown;
}
