/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BlockchainAdapter.h"
#include <spdlog/spdlog.h>
#include "ArmoryErrors.h"
#include "BitcoinFeeCache.h"

#include "common.pb.h"

using namespace BlockSettle::Common;
using namespace bs::message;

static const auto kReconnectInterval = std::chrono::seconds{ 30 };
static const auto kBroadcastTimeout = std::chrono::seconds{ 30 };


BinaryData TxWithHeight::serialize() const
{
   BinaryData result;
   result.append(WRITE_UINT32_LE(txHeight_));
   result.append(Tx::serialize());
   return result;
}

void TxWithHeight::deserialize(const BinaryData &data)
{
   txHeight_ = READ_UINT32_LE(data.getSliceRef(0, sizeof(uint32_t)));
   Tx::unserialize(data.getSliceRef(sizeof(uint32_t), data.getSize() - sizeof(uint32_t)));
}


BlockchainAdapter::BlockchainAdapter(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::message::User> &user
   , const std::shared_ptr<ArmoryConnection> &armory) //pre-inited armory connection [optional]
   : logger_(logger), user_(user), armoryPtr_(armory)
{}

bool BlockchainAdapter::process(const bs::message::Envelope &env)
{
   if (env.receiver->value() == user_->value()) {
      ArmoryMessage msg;
      if (!msg.ParseFromString(env.message)) {
         logger_->error("[{}] failed to parse own request #{}", __func__, env.id);
         return true;
      }
      switch (msg.data_case()) {
      case ArmoryMessage::kReconnect:
         armoryPtr_.reset();
         start();
         break;
      case ArmoryMessage::kSettingsResponse:
         return processSettings(msg.settings_response());
      case ArmoryMessage::kRegisterWallet:
         break;
      case ArmoryMessage::kTxPush:
         return processPushTxRequest(env, msg.tx_push());
      case ArmoryMessage::kTxPushTimeout:
         onBroadcastTimeout(msg.tx_push_timeout());
         break;
      case ArmoryMessage::kSetUnconfTarget:
         return processUnconfTarget(env, msg.set_unconf_target());
      case ArmoryMessage::kAddrTxnRequest:
         return processGetTxNs(env, msg.addr_txn_request());
      case ArmoryMessage::kWalletBalanceRequest:
         return processBalance(env, msg.wallet_balance_request());
      case ArmoryMessage::kGetTxsByHash:
         return processGetTXsByHash(env, msg.get_txs_by_hash());
      default:
         logger_->warn("[{}] unknown message to blockchain #{}: {}", __func__
            , env.id, msg.data_case());
         break;
      }
   }
   return true;
}

void BlockchainAdapter::setQueue(const std::shared_ptr<bs::message::QueueInterface> &queue)
{
   Adapter::setQueue(queue);
   start();
}

void BlockchainAdapter::start()
{
   if (!armoryPtr_) {
      armoryPtr_ = std::make_shared<ArmoryConnection>(logger_);

      ArmoryMessage msg;
      msg.mutable_settings_request();  // broadcast ask for someone to provide settings
      bs::message::Envelope env{ 0, user_, nullptr, {}, {}
         , msg.SerializeAsString(), true };
      pushFill(env);
   }
   else {
      sendLoadingBC();
   }
   init(armoryPtr_.get());
   feeEstimationsCache_ = std::make_shared<BitcoinFeeCache>(logger_, armoryPtr_);
}

void BlockchainAdapter::sendReady()
{
   ArmoryMessage msg;
   msg.mutable_ready();
   Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void BlockchainAdapter::sendLoadingBC()
{
   ArmoryMessage msg;
   msg.mutable_loading();
   Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

bool BlockchainAdapter::processSettings(const ArmoryMessage_Settings &settings)
{
   BinaryData serverBIP15xKey;
   if (!settings.bip15x_key().empty()) {
      try {
         serverBIP15xKey = READHEX(settings.bip15x_key());
      } catch (const std::exception &e) {
         logger_->error("[BlockchainAdapter::processSettings] invalid armory key detected: {}: {}"
            , settings.bip15x_key(), e.what());
         return true;
      }
   }

   armoryPtr_->setupConnection(static_cast<NetworkType>(settings.network_type())
      , settings.host(), settings.port(), settings.data_dir(), serverBIP15xKey);
   return true;
}

void BlockchainAdapter::reconnect()
{
   logger_->debug("[BlockchainAdapter::reconnect]");
   ArmoryMessage msg;
   msg.mutable_reconnect();
   const auto timeNow = std::chrono::system_clock::now();
   bs::message::Envelope env{ 0, user_, user_, timeNow
      , timeNow + kReconnectInterval, msg.SerializeAsString() };
   pushFill(env);
}

void BlockchainAdapter::resumeRegistrations()
{
   for (const auto &wallet : wallets_) {
      registerWallet(wallet.first, wallet.second);
   }
}

void BlockchainAdapter::onStateChanged(ArmoryState st)
{
   switch (st) {
   case ArmoryState::Ready:
      resumeRegistrations();
      sendReady();
      break;

   case ArmoryState::Connected:
      armoryPtr_->goOnline();
      break;

   case ArmoryState::Error:
      logger_->error("[BlockchainAdapter::onStateChanged] armory connection encountered some errors");
      [[clang::fallthrough]];
   case ArmoryState::Offline:
      logger_->info("[BlockchainAdapter::onStateChanged] Armory is offline - "
         "suspended and reconnecting");

      reconnect();
      [[clang::fallthrough]];
   default:
//      settlStartRequests_.clear();
      break;
   }

   ArmoryMessage msg;
   auto msgState = msg.mutable_state_changed();
   msgState->set_state(static_cast<int32_t>(st));
   msgState->set_top_block(armory_->topBlock());
   bs::message::Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void BlockchainAdapter::onRefresh(const std::vector<BinaryData> &ids, bool online)
{
   ArmoryMessage msg;
   auto msgRefresh = msg.mutable_refresh();
   for (const auto &id : ids) {
      const auto &idStr = id.toBinStr();
      const auto &itReg = regMap_.find(idStr);
      if (itReg != regMap_.end()) {
         ArmoryMessage msgReg;
         auto msgWalletRegged = msgReg.mutable_wallet_registered();
         msgWalletRegged->set_wallet_id(itReg->second);
         const auto &itRegReq = reqByRegId_.find(idStr);
         Envelope env;
         if (itRegReq == reqByRegId_.end()) {
            env = Envelope{ 0, user_, nullptr, {}, {}, msgReg.SerializeAsString() };
         }
         else {
            env = Envelope{ itRegReq->second.id, user_, itRegReq->second.sender
               , {}, {}, msgReg.SerializeAsString() };
            reqByRegId_.erase(itRegReq);
         }
         pushFill(env);
         regMap_.erase(itReg);
         continue;
      }

      const auto &itUnconfTgt = unconfTgtMap_.find(idStr);
      if (itUnconfTgt != unconfTgtMap_.end()) {
         ArmoryMessage msgUnconfTgt;
         msgUnconfTgt.set_unconf_target_set(itUnconfTgt->second.first);
         bs::message::Envelope env{ itUnconfTgt->second.second.id, user_
            , itUnconfTgt->second.second.sender, {}, {}
            , msgUnconfTgt.SerializeAsString() };
         pushFill(env);
         unconfTgtMap_.erase(itUnconfTgt);
         continue;
      }
      msgRefresh->add_ids(id.toBinStr());
   }
   //TODO: resume all pending requests if all wallets registered
   if (msgRefresh->ids_size() > 0) {
      msgRefresh->set_online(online);
      bs::message::Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
      pushFill(env);
   }
}

void BlockchainAdapter::onNewBlock(unsigned int height, unsigned int branchHeight)
{
   ArmoryMessage msg;
   auto msgBlock = msg.mutable_new_block();
   msgBlock->set_top_block(height);
   msgBlock->set_branch_height(branchHeight);
   bs::message::Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void BlockchainAdapter::onZCInvalidated(const std::set<BinaryData> &ids)
{
   ArmoryMessage msg;
   auto zcInv = msg.mutable_zc_invalidated();
   for (const auto &id : ids) {
      zcInv->add_tx_hashes(id.toBinStr());
   }
   bs::message::Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void BlockchainAdapter::onZCReceived(const std::string &requestId, const std::vector<bs::TXEntry> &entries)
{
   ArmoryMessage msg;
   auto msgZC = msg.mutable_zc_received();
   msgZC->set_request_id(requestId);
   for (const auto &entry : entries) {
      //TODO: probably need to handle PushZC broadcasts/timeouts here as well
      auto msgTX = msgZC->add_tx_entries();

   }
   bs::message::Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

// Unused code to save from PB's ArmoryWalletAdapter
/*constexpr auto kTxRebroadcastInterval = std::chrono::seconds(30);
namespace KnownBitcoinCoreErrors
{
   //                  Successfully broadcasted error codes
   // AlreadyInMemPool - means that TX is already in mempool
   // treat as broadcast was successfull
   const std::string TxAlreadyInMemPool = "txn-already-in-mempool";

   //                   TX conflicts error codes
   // TxAlreadyKnown - looks like there is already TX for those inputs
   // treat as broadcast was successfull. If it is a double spend - will be detected
   // by settlement monitor.
   const std::string TxAlreadyKnown = "txn-already-known";

   // TxMempoolConflict - there is TX in a mmepool that try to spent payin inputs
   // should be monitored before release. In case if this is pay-out - it
   // might be a revoke TX. should be monitored as well
   const std::string TxMempoolConflict = "txn-mempool-conflict";

   //                rebroadcast error codes
   // MempoolFool - TX was not accepted, but it might be accepted in a future
   // reason - there is just not enough space in a mempool right now
   const std::string MempoolFull = "mempool full";

   // no need to add this errors. Any other should be a good enough reason to cancel reservation
   //A transaction that spends outputs that would be replaced by it is invalid
   //TxValidationResult::TX_CONSENSUS, "bad-txns-spends-conflicting-tx"
}*/

void BlockchainAdapter::onTxBroadcastError(const std::string& requestId
   , const BinaryData &txHash, int errCode, const std::string &errMsg)
{
   ArmoryMessage msg;
   auto msgResp = msg.mutable_tx_push_result();
   msgResp->set_request_id(requestId);
   msgResp->set_tx_hash(txHash.toBinStr());
   msgResp->set_error_message(errMsg);

   const auto &txHashString = txHash.toHexStr(true);
   auto broadcastErrCode = (ArmoryErrorCodes)errCode;

   switch (broadcastErrCode)
   {
   case ArmoryErrorCodes::ZcBroadcast_AlreadyInChain:
      //tx is already mined
      logger_->debug("[BlockchainAdapter::onTxBroadcastError] {} {} already in chain."
         , requestId, txHashString);
      msgResp->set_result(ArmoryMessage::PushTxAlreadyInChain);
      break;

   case ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool:
      //tx was broadcasted succesfully by another party
      logger_->debug("[BlockchainAdapter::onTxBroadcastError] {} {} already in "
         "mempool - processing as broadcasted", requestId, txHashString);
      msgResp->set_result(ArmoryMessage::PushTxAlreadyInMempool);
      break;

   case ArmoryErrorCodes::P2PReject_Duplicate:
      //mempool double spend
      logger_->error("[BlockchainAdapter::onTxBroadcastError] {} {} - {}. "
         "Double spend", requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxMempoolConflict);
      break;

   case ArmoryErrorCodes::ZcBatch_Timeout:
      onBroadcastTimeout(txHash.toBinStr());
      return;

   case ArmoryErrorCodes::ZcBroadcast_Error:
      //failed consensus rules, this tx cannot be mined
      logger_->error("[BlockchainAdapter::onTxBroadcastError] {} {} - {}. "
         "Breaks consensus rules", requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
      break;

   case ArmoryErrorCodes::ZcBroadcast_VerifyRejected:
      /*
      Failed verification: bad sig / malformed tx / utxo spent by other zc
      Since we check for signature and tx structure validity, this error is always treated as
      a mempool conflict
      */
      logger_->error("[ArmoryWalletAdapter::onTxBroadcastError] {} {} - {}. Possible"
         " double spend", requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxMempoolConflict);
      break;

   case ArmoryErrorCodes::P2PReject_InsufficientFee:
      //breaks propagation rules (typically RBF fee failures)
      logger_->error("[ArmoryWalletAdapter::onTxBroadcastError] {} {} - {}"
         , requestId, txHashString, errMsg);
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
      break;

   default:
      //report and fail on errors that aren't specifically handled
      logger_->error("[ArmoryWalletAdapter::onTxBroadcastError] {} {} - {} - "
         "errCode: {}. Unhandled error", requestId, txHashString, errMsg, errCode);
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
   }
   bs::message::Envelope env{ 0, user_, nullptr, {}, {}, msg.SerializeAsString() };
   pushFill(env);
}

void BlockchainAdapter::suspend()
{
   suspended_ = true;

   for (auto &wallet : wallets_) {
      wallet.second.registered = false;
      wallet.second.wallet.reset();
   }
}

void BlockchainAdapter::onBroadcastTimeout(const std::string &timeoutId)
{
   logger_->info("[BlockchainAdapter::onBroadcastTimeout] {}",timeoutId);
   suspend();
   reconnect();
}

bool BlockchainAdapter::processRegisterWallet(const bs::message::Envelope &env
   , const ArmoryMessage_RegisterWallet &request)
{
   const auto &sendError = [this, env, request]
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_wallet_registered();
      msgResp->set_wallet_id(request.wallet_id());
      msgResp->set_success(false);
      bs::message::Envelope envResp{ env.id, user_, env.sender, {}, {}
         , msg.SerializeAsString() };
      pushFill(envResp);
   };
   Wallet wallet;
   wallet.asNew = request.as_new();
   try {
      for (const auto &addrStr : request.addresses()) {
         wallet.addresses.push_back(BinaryData::fromString(addrStr));
      }
   }
   catch (const std::exception &e) {
      logger_->error("[BlockchainAdapter::processRegisterWallet] failed to deser"
         " address: {}", e.what());
      sendError();
   }
   const auto &regId = registerWallet(request.wallet_id(), wallet);
   if (regId.empty()) {
      sendError();
   }
   else {
      reqByRegId_[regId] = env;
   }
   return true;
}

std::string BlockchainAdapter::registerWallet(const std::string &walletId
   , const Wallet &wallet)
{
   auto &newWallet = wallets_[walletId];
   if (!newWallet.wallet) {
      newWallet.wallet = armoryPtr_->instantiateWallet(walletId);
   }
   newWallet.asNew = wallet.asNew;
   newWallet.addresses = std::move(wallet.addresses);

   const auto &regId = newWallet.wallet->registerAddresses(newWallet.addresses
      , wallet.asNew);
   regMap_[regId] = walletId;
   return regId;
}

bool BlockchainAdapter::processUnconfTarget(const bs::message::Envelope &env
   , const ArmoryMessage_WalletUnconfirmedTarget &request)
{
   const auto &itWallet = wallets_.find(request.wallet_id());
   if (itWallet == wallets_.end()) {
      logger_->error("[{}] unknown wallet {}", __func__, request.wallet_id());
      return true;
   }
   if (!itWallet->second.registered) {
      logger_->warn("[{}] wallet {} is not registered, yet", __func__
         , request.wallet_id());
      return false;
   }
   std::string regId;
   if (itWallet->second.wallet) {
      regId = itWallet->second.wallet->setUnconfirmedTarget(request.conf_count());
   }
   if (regId.empty()) {
      logger_->error("[{}] invalid wallet {}", __func__, request.wallet_id());
      return true;
   }
   unconfTgtMap_[regId] = { request.wallet_id(), env };
   return true;
}

bool BlockchainAdapter::processGetTxNs(const bs::message::Envelope &env
   , const ArmoryMessage_WalletIDs &request)
{
   const auto &cbTxNs = [this, env]
      (const std::map<std::string, CombinedCounts> &txns)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_addr_txn_response();
      for (const auto &txn : txns) {
         auto msgByWallet = msgResp->add_wallet_txns();
         msgByWallet->set_wallet_id(txn.first);
         for (const auto &byAddr : txn.second.addressTxnCounts_) {
            auto msgByAddr = msgByWallet->add_txns();
            msgByAddr->set_address(byAddr.first.toBinStr());
            msgByAddr->set_txn(byAddr.second);
         }
      }
      Envelope envResp{ env.id, user_, env.sender, {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
   };
   std::vector<std::string> walletIDs;
   walletIDs.reserve(request.wallet_ids_size());
   for (const auto &walletId : request.wallet_ids()) {
      walletIDs.push_back(walletId);
   }
   return armoryPtr_->getCombinedTxNs(walletIDs, cbTxNs);
}

bool BlockchainAdapter::processBalance(const bs::message::Envelope &env
   , const ArmoryMessage_WalletIDs &request)
{
   const auto &cbBal = [this, env]
      (const std::map<std::string, CombinedBalances> &bals)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_wallet_balance_response();
      for (const auto &bal : bals) {
         auto msgByWallet = msgResp->add_balances();
         msgByWallet->set_wallet_id(bal.first);
         if (bal.second.walletBalanceAndCount_.size() == 4) {
            msgByWallet->set_full_balance(bal.second.walletBalanceAndCount_[0]);
            msgByWallet->set_spendable_balance(bal.second.walletBalanceAndCount_[1]);
            msgByWallet->set_unconfirmed_balance(bal.second.walletBalanceAndCount_[2]);
            msgByWallet->set_address_count(bal.second.walletBalanceAndCount_[3]);
         }
         else {
            logger_->warn("[BlockchainAdapter::processBalance] empty wallet "
               "balance received for {}", bal.first);
         }
         for (const auto &byAddr : bal.second.addressBalances_) {
            auto msgByAddr = msgByWallet->add_addr_balances();
            msgByAddr->set_address(byAddr.first.toBinStr());
            if (byAddr.second.size() == 3) {
               msgByAddr->set_full_balance(byAddr.second[0]);
               msgByAddr->set_spendable_balance(byAddr.second[1]);
               msgByAddr->set_unconfirmed_balance(byAddr.second[2]);
            }
            else {
               logger_->warn("[BlockchainAdapter::processBalance] empty address"
                  " balance received for {}/{}", bal.first, byAddr.first.toHexStr());
            }
         }
      }
      Envelope envResp{ env.id, user_, env.sender, {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
   };
   std::vector<std::string> walletIDs;
   walletIDs.reserve(request.wallet_ids_size());
   for (const auto &walletId : request.wallet_ids()) {
      walletIDs.push_back(walletId);
   }
   return armoryPtr_->getCombinedBalances(walletIDs, cbBal);
}

bool BlockchainAdapter::processPushTxRequest(const bs::message::Envelope &env
   , const ArmoryMessage_TXPushRequest &request)
{
   if (suspended_) {
      logger_->debug("[BlockchainAdapter::processPushTxRequest] suspended");
      std::unique_lock<std::mutex> lock(mtxReqPool_);
      requestsPool_[env.id] = env;
      return true;
   }

   const auto &sendError = [this, request, env](const std::string &errMsg) -> bool
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_tx_push_result();
      msgResp->set_push_id(request.push_id());
      msgResp->set_result(ArmoryMessage::PushTxOtherError);
      msgResp->set_error_message(errMsg);
      bs::message::Envelope envResp{ env.id, user_, env.sender, {}, {}
         , msg.SerializeAsString() };
      return pushFill(envResp);
   };
   std::vector<BinaryData> txToPush;

   for (const auto &tx : request.txs_to_push()) {
      const auto &binTX = BinaryData::fromString(tx.tx());
      const Tx txObj(binTX);
      if (binTX.empty() || !txObj.isInitialized()) {
         logger_->error("[BlockchainAdapter::processPushTxRequest] invalid TX "
            "data to push");
         return sendError("invalid TX data");
      }

      auto txHash = BinaryData::fromString(tx.expected_tx_hash());
      if (txHash.empty()) {
         txHash = txObj.getThisHash();
      }
      else {
         if (txHash != txObj.getThisHash()) {
            return sendError("TX hash mismatch");
         }
      }
   }

   const auto &pushRequestId = (txToPush.size() == 1) ?
      armory_->pushZC(txToPush[0]) : armory_->pushZCs(txToPush);
   if (pushRequestId.empty()) {
      logger_->error("[BlockchainAdapter::processPushTxRequest] failed to push TX");
      return sendError("failed to push");
   }
   sendBroadcastTimeout(pushRequestId);

   logger_->debug("[BlockchainAdapter::processPushTxRequest] pushed id {} for"
      " request {}", pushRequestId, request.push_id());
   return true;
}

void BlockchainAdapter::sendBroadcastTimeout(const std::string &timeoutId)
{
   ArmoryMessage msg;
   msg.set_tx_push_timeout(timeoutId);
   const auto &timeNow = std::chrono::system_clock::now();
   bs::message::Envelope env{ 0, user_, user_, timeNow
      , timeNow + kBroadcastTimeout, msg.SerializeAsString() };
   pushFill(env);
}

bool BlockchainAdapter::processGetTXsByHash(const bs::message::Envelope &env
   , const ArmoryMessage_TXHashes &request)
{
   const auto &cb = [this, env]
      (const AsyncClient::TxBatchResult &txBatch, std::exception_ptr)
   {
      ArmoryMessage msg;
      auto msgResp = msg.mutable_transactions();
      for (const auto &tx : txBatch) {
         msgResp->add_transactions(tx.second->serialize().toBinStr());
      }             // broadcast intentionally even as a reply to some request
      Envelope envResp{ env.id, user_, nullptr, {}, {}, msg.SerializeAsString() };
      pushFill(envResp);
   };

   std::set<BinaryData> hashes;
   for (const auto &hash : request.tx_hashes()) {
      hashes.insert(BinaryData::fromString(hash));
   }
   return armoryPtr_->getTXsByHash(hashes, cb, !request.disable_cache());
}
