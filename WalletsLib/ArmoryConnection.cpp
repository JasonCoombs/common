/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ArmoryConnection.h"

#include <cassert>
#include <exception>
#include <condition_variable>
#include <spdlog/spdlog.h>

#include "ArmoryErrors.h"
#include "DbHeader.h"
#include "EncryptionUtils.h"
#include "JSON_codec.h"
#include "ManualResetEvent.h"
#include "ScopedFlag.h"
#include "SocketIncludes.h"


ArmoryCallbackTarget::ArmoryCallbackTarget()
{}

ArmoryCallbackTarget::~ArmoryCallbackTarget()
{
   assert(!armory_);
}

void ArmoryCallbackTarget::init(ArmoryConnection *armory)
{
   if (!armory_ && armory) {
      armory_ = armory;
      armory_->addTarget(this);
   }
}

void ArmoryCallbackTarget::cleanup()
{
   if (armory_) {
      armory_->removeTarget(this);
      onDestroy();
   }
}

void ArmoryCallbackTarget::onDestroy()
{
   armory_ = nullptr;
}


ArmoryConnection::ArmoryConnection(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{
   maintThreadRunning_ = true;
   thread_ = std::thread(&ArmoryConnection::threadFunction, this);
}

ArmoryConnection::~ArmoryConnection() noexcept
{
   shutdown();
   for (const auto &tgt : activeTargets_) {
      tgt->onDestroy();
   }
}

bool ArmoryConnection::addTarget(ArmoryCallbackTarget *act)
{
   std::unique_lock<std::mutex> lock(cbMutex_);
   const auto &it = activeTargets_.find(act);
   if (it != activeTargets_.end()) {
      logger_->warn("[ArmoryConnection::addTarget] target {} already exists", (void*)act);
      return false;
   }
   activeTargets_.insert(act);
   return true;
}

bool ArmoryConnection::removeTarget(ArmoryCallbackTarget *act)
{
   std::promise<bool> done;
   auto doneFut = done.get_future();

   runOnMaintThread([this, &done, act] () {
      std::unique_lock<std::mutex> lock(cbMutex_);
      const auto &it = activeTargets_.find(act);
      if (it == activeTargets_.end()) {
         logger_->warn("[ArmoryConnection::removeTarget] target {} wasn't added", (void*)act);
         done.set_value(false);
         return;
      }
      activeTargets_.erase(it);
      done.set_value(true);
   });

   if (doneFut.wait_for(std::chrono::seconds{ 1 }) == std::future_status::ready) {
      return doneFut.get();
   }
   return false;
}

void ArmoryConnection::threadFunction()
{
   const auto &forEachTarget = [this](const std::function<void(ArmoryCallbackTarget *tgt)> &cb) {
      decltype(activeTargets_) notifiedACTs;

      {
         std::unique_lock<std::mutex> lock(cbMutex_);
         notifiedACTs = activeTargets_;
      }

      for (const auto &tgt : notifiedACTs) {
         if (!maintThreadRunning_) {
            break;
         }
         cb(tgt);
      }
   };

   while (maintThreadRunning_) {
      {
         std::unique_lock<std::mutex> lock(actMutex_);
         if (actQueue_.empty() && runQueue_.empty()) {
            actCV_.wait(lock);
         }
      }
      if (!maintThreadRunning_) {
         break;
      }

      decltype(actQueue_) tempQueue;
      decltype(runQueue_) tempRunQueue;
      {
         std::unique_lock<std::mutex> lock(actMutex_);
         tempQueue.swap(actQueue_);
         tempRunQueue.swap(runQueue_);
      }

      for (const auto &cb : tempRunQueue) {
         cb();
      }

      for (const auto &cb : tempQueue) {
         forEachTarget(cb);
         if (!maintThreadRunning_) {
            break;
         }
      }
   }
}

void ArmoryConnection::addToQueue(const CallbackQueueCb &cb)
{
   std::unique_lock<std::mutex> lock(actMutex_);
   actQueue_.push_back(cb);
   actCV_.notify_one();
}

void ArmoryConnection::runOnMaintThread(ArmoryConnection::EmptyCb cb)
{
   if (std::this_thread::get_id() == thread_.get_id() || !maintThreadRunning_) {
      cb();
      return;
   }

   std::unique_lock<std::mutex> lock(actMutex_);
   runQueue_.push_back(std::move(cb));
   actCV_.notify_one();
}

void ArmoryConnection::stopServiceThreads()
{
   regThreadRunning_ = false;
   {
      std::unique_lock<std::mutex> lock(regMutex_);
      regCV_.notify_one();
   }
   if (regThread_.joinable()) {
      regThread_.join();
   }
}

void ArmoryConnection::setupConnection(NetworkType netType
   , const std::string &host, const std::string &port
   , const std::string& datadir
   , bool oneWayAuth, const BIP151Cb &cbBIP151)
{
   if (host.empty()) {
      logger_->error("[ArmoryConnection::setupConnection] invalid connection host");
      return;
   }
   addToQueue([netType, host, port](ArmoryCallbackTarget *tgt) {
      tgt->onPrepareConnection(netType, host, port);
   });

   needsBreakConnectionLoop_.store(false);

   const auto &registerRoutine = [this, netType] {
      logger_->debug("[ArmoryConnection::setupConnection] started");
      while (regThreadRunning_) {
         try {
            registerBDV(netType);
            if (!bdv_->getID().empty()) {
               logger_->debug("[ArmoryConnection::setupConnection] got BDVid: {}", bdv_->getID());
               setState(ArmoryState::Connected);
               break;
            }
         }
         catch (const BDVAlreadyRegistered &) {
            logger_->warn("[ArmoryConnection::setupConnection] BDV already registered");
            break;
         }
         catch (const std::exception &e) {
            logger_->error("[ArmoryConnection::setupConnection] registerBDV exception: {}", e.what());
            setState(ArmoryState::Error);
            addToQueue([e](ArmoryCallbackTarget *tgt) {
               tgt->onError(static_cast<int>(ErrorCodes::BDV_Error), e.what());
            });
         }
         catch (...) {
            logger_->error("[ArmoryConnection::setupConnection] registerBDV exception");
            setState(ArmoryState::Error);
            addToQueue([](ArmoryCallbackTarget *tgt) {
               tgt->onError(static_cast<int>(ErrorCodes::BDV_Error), {});
            });
         }

         std::unique_lock<std::mutex> lock(regMutex_);
         regCV_.wait_for(lock, std::chrono::seconds{ 10 });
      }
      regThreadRunning_ = false;
      logger_->debug("[ArmoryConnection::setupConnection] completed");
   };

   const auto &connectRoutine = [this, registerRoutine, oneWayAuth, cbBIP151, host, port, datadir] {
      if (connThreadRunning_) {
         return;
      }

      ScopedFlag<decltype(connThreadRunning_)> f{connThreadRunning_};
      setState(ArmoryState::Connecting);
      stopServiceThreads();

      if (bdv_) {
         bdv_->unregisterFromDB();
         bdv_.reset();
      }
      isOnline_ = false;
      if (needsBreakConnectionLoop_.load()) {
         setState(ArmoryState::Cancelled);
         return;
      }
      if (!cbRemote_) {
         cbRemote_ = std::make_shared<ArmoryCallback>(this, logger_);
      }
      logger_->debug("[ArmoryConnection::setupConnection] connecting to Armory {}:{}"
                     , host, port);

      auto passLbd = [](const std::set<Armory::Wallets::EncryptionKeyId>&)->SecureBinaryData
      {
         //return empty passphase, we don't want to lock the key-store wallet (TODO: revisit)
         return SecureBinaryData();
      };

      // Get Armory BDV (gateway to the remote ArmoryDB instance). cbBIP151
      // will deal with key ACK/nACK. BIP 150/151 is transparent to us
      // otherwise. If it fails, the connection will fail.
      bdv_ = AsyncClient::BlockDataViewer::getNewBDV(host, port
         , datadir
         , passLbd

         /*if cbBIP151 is set, use it and ignore key store (ephemeral peers)*/
         , cbBIP151 != nullptr 

         , oneWayAuth
         , cbRemote_);
      if (!bdv_) {
         logger_->error("[setupConnection (connectRoutine)] failed to "
            "create BDV");
         setState(ArmoryState::Offline);
         return;
      }

      //set the key management lambda
      bdv_->setCheckServerKeyPromptLambda(cbBIP151);

      //connect
      if (!bdv_->connectToRemote()) {
         logger_->error("[ArmoryConnection::setupConnection] BDV connection failed");
         setState(ArmoryState::Offline);
         return;
      }
      logger_->debug("[ArmoryConnection::setupConnection] BDV connected");

      regThreadRunning_ = true;
      regThread_ = std::thread(registerRoutine);
   };
   std::thread(connectRoutine).detach();
}

bool ArmoryConnection::goOnline()
{
   if ((state_ != ArmoryState::Connected) || !bdv_) {
      logger_->error("[ArmoryConnection::goOnline] invalid state: {}"
                     , static_cast<int>(state_.load()));
      return false;
   }
   logger_->debug("[ArmoryConnection::goOnline]");
   bdv_->goOnline();
   isOnline_ = true;
   return true;
}

void ArmoryConnection::registerBDV(NetworkType netType)
{
   BinaryData magicBytes;
   switch (netType) {
   case NetworkType::TestNet:
      magicBytes = READHEX(TESTNET_MAGIC_BYTES);
      break;
   case NetworkType::RegTest:
      magicBytes = READHEX(REGTEST_MAGIC_BYTES);
      break;
   case NetworkType::MainNet:
      magicBytes = READHEX(MAINNET_MAGIC_BYTES);
      break;
   default:
      throw std::runtime_error("unknown network type");
   }
   bdv_->registerWithDB(magicBytes);
}

void ArmoryConnection::setTopBlock(unsigned int topBlock)
{
   topBlock_ = topBlock;
}

void ArmoryConnection::setState(ArmoryState state)
{
   if (state_ != state) {
      logger_->debug("[ArmoryConnection::setState] from {} to {}", (int)state_.load(), (int)state);
      state_ = state;
      addToQueue([state](ArmoryCallbackTarget *tgt) {
         tgt->onStateChanged(static_cast<ArmoryState>(state));
      });
   }
}

std::string ArmoryConnection::broadcastZC(const BinaryData& rawTx)
{
   if (!bdv_ || ((state_ != ArmoryState::Ready) && (state_ != ArmoryState::Connected))) {
      logger_->error("[ArmoryConnection::broadcastZC] invalid state: {} (BDV null: {})"
         , (int)state_.load(), (bdv_ == nullptr));
      return "";
   }

   if (rawTx.empty()) {
      SPDLOG_LOGGER_ERROR(logger_, "broadcast failed: empty rawTx");
      return "";
   }

   try
   {
      Tx tx(rawTx);
      if (!tx.isInitialized() || tx.getThisHash().empty()) {
         logger_->error("[ArmoryConnection::broadcastZC] invalid TX data (size {}) - aborting broadcast"
                        , rawTx.getSize());
         return "";
      }
   } catch (const BlockDeserializingException &e) {
      SPDLOG_LOGGER_ERROR(logger_, "broadcast failed: BlockDeserializingException, details: '{}'", e.what());
      return "";
   } catch (const std::exception &e) {
      SPDLOG_LOGGER_ERROR(logger_, "broadcast failed: {}", e.what());
      return "";
   }

   SPDLOG_LOGGER_DEBUG(logger_, "broadcast new TX: {}", rawTx.toHexStr());

   return bdv_->broadcastZC(rawTx);
}

bool ArmoryConnection::getWalletsHistory(const std::vector<std::string> &walletIDs
   , const WalletsHistoryCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getWalletsHistory] invalid state: {}", (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb]
      (ReturnMessage<std::vector<DBClientClasses::LedgerEntry>> entries)
   {
      try {
         if (cb) {
            cb(entries.get());
         }
      }
      catch(std::exception& e) {
         logger->error("[ArmoryConnection::getWalletsHistory (cbWrap)] Return data error - {}"
            , e.what());
      }
   };

   bdv_->getHistoryForWalletSelection(walletIDs, "ascending", cbWrap);
   return true;
}

bool ArmoryConnection::getLedgerDelegateForAddress(const std::string &walletId, const bs::Address &addr)
{
   const auto &cbWrap = [this, walletId, addr]
      (const std::shared_ptr<AsyncClient::LedgerDelegate> &delegate)
   {
      addToQueue([addr, delegate] (ArmoryCallbackTarget *tgt) {
         tgt->onLedgerForAddress(addr, delegate);
      });
   };
   return getLedgerDelegateForAddress(walletId, addr, cbWrap);
}

bool ArmoryConnection::getLedgerDelegateForAddress(const std::string &walletId
   , const bs::Address &addr
   , const std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)> &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getLedgerDelegateForAddress] invalid state: {}", (int)state_.load());
      return false;
   }
   const auto &cbWrap = [this, cb, walletId, addr]
      (ReturnMessage<AsyncClient::LedgerDelegate> delegate)
   {
      try {
         auto ld = std::make_shared<AsyncClient::LedgerDelegate>(delegate.get());
         if (cb) {
            cb(ld);
         }
      } catch (const std::exception &e) {
         logger_->error("[ArmoryConnection::getLedgerDelegateForAddress (cbWrap)] Return data "
            "error - {} - Wallet {} - Address {}", e.what(), walletId
            , addr.empty() ? "<empty>" : addr.display());
         if (cb) {
            cb(nullptr);
         }
      }
   };
   logger_->debug("[{}] {}.{} ({})", __func__, walletId, addr.display(), addr.id().toHexStr());
   bdv_->getLedgerDelegateForScrAddr(walletId, addr.id(), cbWrap);
   return true;
}

bool ArmoryConnection::getWalletsLedgerDelegate(const LedgerDelegateCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getWalletsLedgerDelegate] invalid state: {}", (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb](ReturnMessage<AsyncClient::LedgerDelegate> delegate) {
      try {
         auto ld = std::make_shared< AsyncClient::LedgerDelegate>(delegate.get());
         if (cb) {
            cb(ld);
         }
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getWalletsLedgerDelegate (cbWrap)] Return data error "
            "- {}", e.what());
         if (cb) {
            cb(nullptr);
         }
      }
   };
   bdv_->getLedgerDelegateForWallets(cbWrap);
   return true;
}

bool ArmoryConnection::getSpendableTxOutListForValue(const std::vector<std::string> &walletIds
   , uint64_t val, const UTXOsCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getSpendableTxOutListForValue] invalid state: {}", (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb](ReturnMessage<std::vector<UTXO>> retMsg) {
      try {
         const auto &txOutList = retMsg.get();
         if (cb) {
            cb(txOutList);
         }
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getSpendableTxOutListForValue] failed: {}", e.what());
         if (cb) {
            cb({});
         }
      }
   };
   bdv_->getCombinedSpendableTxOutListForValue(walletIds, val, cbWrap);
   return true;
}

bool ArmoryConnection::getSpendableZCoutputs(const std::vector<std::string> &walletIds, const UTXOsCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getSpendableZCoutputs] invalid state: {}", (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb](ReturnMessage<std::vector<UTXO>> retMsg) {
      try {
         const auto &txOutList = retMsg.get();
         if (cb) {
            cb(txOutList);
         }
      } catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getSpendableZCoutputs] failed: {}", e.what());
         if (cb) {
            cb({});
         }
      }
   };

   bdv_->getCombinedSpendableZcOutputs(walletIds, cbWrap);
   return true;
}

bool ArmoryConnection::getNodeStatus(const std::function<void(const std::shared_ptr<DBClientClasses::NodeStatus>)>& userCB)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getNodeStatus] invalid state: {}", (int)state_.load());
      return false;
   }

   if (!userCB) {
      logger_->error("[ArmoryConnection::getNodeStatus] invalid callback");
      return false;
   }

   const auto cbWrap = [logger=logger_, userCB]
      (ReturnMessage<std::shared_ptr<DBClientClasses::NodeStatus>> reply)
   {
      try {
         const auto nodeStatus = reply.get();
         userCB(nodeStatus);
      } catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getNodeStatus] failed: {}", e.what());
         userCB({});
      }
   };
   bdv_->getNodeStatus(cbWrap);
   return true;
}

bool ArmoryConnection::getRBFoutputs(const std::vector<std::string> &walletIds, const UTXOsCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb](ReturnMessage<std::vector<UTXO>> retMsg) {
      try {
         const auto &txOutList = retMsg.get();
         if (cb) {
            cb(txOutList);
         }
      } catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getRBFoutputs] failed: {}", e.what());
         if (cb) {
            cb({});
         }
      }
   };
   bdv_->getCombinedRBFTxOuts(walletIds, cbWrap);
   return true;
}

bool ArmoryConnection::getUTXOsForAddress(const BinaryData &addr, const UTXOsCb &cb, bool withZC)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb, addr](ReturnMessage<std::vector<UTXO>> retMsg) {
      try {
         const auto &utxos = retMsg.get();
         if (cb) {
            cb(utxos);
         }
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getUTXOsForAddress] {} failed: {}"
            , addr.toHexStr(), e.what());
         if (cb) {
            cb({});
         }
      }
   };
   bdv_->getUTXOsForAddress(addr, withZC, cbWrap);
   return true;
}

bool ArmoryConnection::getOutpointsFor(const std::vector<BinaryData> &addresses
   , const std::function<void(const OutpointBatch &)> &cb
   , unsigned int height, unsigned int zcIndex)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }

   std::set<BinaryData> addrVec;
   for (const auto &addr : addresses) {
      addrVec.insert(addr);
   }

   const auto cbWrap = [logger=logger_, cb, addresses](ReturnMessage<OutpointBatch> opBatch)
   {
      try {
         const auto batch = opBatch.get();
         if (cb) {
            cb(batch);
         }
      } catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getOutpointsFor] {} address[es] failed: {}"
            , addresses.size(), e.what());
         if (cb) {
            cb({});
         }
      }
   };
   bdv_->getOutpointsForAddresses(addrVec, height, zcIndex, cbWrap);
   return true;
}

bool ArmoryConnection::getOutpointsForAddresses(const std::set<BinaryData> &addrVec
   , const std::function<void(const OutpointBatch &, std::exception_ptr)> &cb
   , unsigned int height, unsigned int zcIndex)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }

   const auto cbWrap = [logger=logger_, cb, addrVec](ReturnMessage<OutpointBatch> opBatch)
   {
      try {
         const auto batch = opBatch.get();
         if (cb) {
            cb(batch, nullptr);
         }
      } catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getOutpointsFor] {} address[es] failed: {}"
            , addrVec.size(), e.what());
         if (cb) {
            cb({}, std::make_exception_ptr(e));
         }
      }
   };
   bdv_->getOutpointsForAddresses(addrVec, height, zcIndex, cbWrap);
   return true;
}

bool ArmoryConnection::getSpentnessForOutputs(const std::map<BinaryData, std::set<unsigned>> &outputs
   , const SpentnessCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }
   const auto cbWrap = [logger = logger_, cb](ReturnMessage<std::map<BinaryData
      , std::map<unsigned int, SpentnessResult>>> msg)
   {
      try {
         const auto &spentness = msg.get();
         cb(spentness, nullptr);
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getSpentnessForOutputs] failed to get: {}", e.what());
         cb({}, std::make_exception_ptr(e));
      }
   };
   // New ArmoryDB will report error if request is empty (expected bindata for getSpentnessForOutputs)
   if (outputs.empty()) {
      cb({}, nullptr);
   } else {
      bdv_->getSpentnessForOutputs(outputs, cbWrap);
   }
   return true;
}

bool ArmoryConnection::getSpentnessForZcOutputs(const std::map<BinaryData, std::set<unsigned>> &outputs
   , const SpentnessCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }
   const auto cbWrap = [logger = logger_, cb](ReturnMessage<std::map<BinaryData
      , std::map<unsigned int, SpentnessResult>>> msg)
   {
      try {
         const auto &spentness = msg.get();
         cb(spentness, nullptr);
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getSpentnessForOutputs] failed to get: {}", e.what());
         cb({}, std::make_exception_ptr(e));
      }
   };
   bdv_->getSpentnessForZcOutputs(outputs, cbWrap);
   return true;
}

bool ArmoryConnection::getOutputsForOutpoints(const std::map<BinaryData
   , std::set<unsigned>> &outpoints, bool withZc,
   const std::function<void(const std::vector<UTXO> &, std::exception_ptr)> &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getOutputsForOutpoints] invalid state: {}"
         , (int)state_.load());
      return false;
   }
   const auto cbWrap = [logger = logger_, cb](ReturnMessage<std::vector<UTXO>> msg)
   {
      try {
         const auto &utxos = msg.get();
         cb(utxos, nullptr);
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getUTXOsForOutpoints] failed to get: {}", e.what());
         cb({}, std::make_exception_ptr(e));
      }
   };
   // ArmoryDB will report error if request is empty (expected bindata for getOutputsForOutpoints)
   if (outpoints.empty()) {
      cb({}, nullptr);
   } else {
      bdv_->getOutputsForOutpoints(outpoints, withZc, cbWrap);
   }
   return true;
}


bool ArmoryConnection::getCombinedBalances(const std::vector<std::string> &walletIDs
   , const std::function<void(const std::map<std::string, CombinedBalances> &)> &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, cb](ReturnMessage<std::map<std::string, CombinedBalances>> retMsg)
   {
      try {
         const auto balances = retMsg.get();
         if (cb) {
            cb(balances);
         }
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getCombinedBalances] failed to get result: {}", e.what());
      }
   };
   bdv_->getCombinedBalances(walletIDs, cbWrap);
   return true;
}

bool ArmoryConnection::getCombinedTxNs(const std::vector<std::string> &walletIDs
   , const std::function<void(const std::map<std::string, CombinedCounts> &)> &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getCombinedTxNs] invalid state: {}", (int)state_.load());
      return false;
   }
   const auto &cbWrap = [logger=logger_, walletIDs, cb](ReturnMessage<std::map<std::string, CombinedCounts>> retMsg)
   {
      try {
         auto counts = retMsg.get();
         if (counts.empty()) {
            for (const auto &id : walletIDs) {
               counts[id] = {};
            }
         }
         if (cb) {
            cb(counts);
         }
      } catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getCombinedTxNs] failed to get result: {}", e.what());
         if (cb) {
            cb({});
         }
      }
   };
   bdv_->getCombinedAddrTxnCounts(walletIDs, cbWrap);
   return true;
}

bool ArmoryConnection::addGetTxCallback(const BinaryData &hash, const TxCb &cb)
{
   std::unique_lock<std::mutex> lock(cbMutex_);

   const auto &it = txCallbacks_.find(hash);
   if (it != txCallbacks_.end()) {
      it->second.push_back(cb);
      return true;
   }

   txCallbacks_[hash].push_back(cb);
   return false;
}

void ArmoryConnection::callGetTxCallbacks(const BinaryData &hash, const AsyncClient::TxResult &tx)
{
   std::vector<TxCb> callbacks;
   {
      std::unique_lock<std::mutex> lock(cbMutex_);
      const auto &it = txCallbacks_.find(hash);
      if (it == txCallbacks_.end()) {
         logger_->error("[{}] no callbacks found for hash {}", __func__
                        , hash.toHexStr(true));
         return;
      }
      callbacks = it->second;
      txCallbacks_.erase(it);
   }
   for (const auto &callback : callbacks) {
      if (callback) {
         callback(tx ? *tx : Tx{});
      }
   }
}

// allowCachedResult is ignored here
bool ArmoryConnection::getTxByHash(const BinaryData &hash, const TxCb &cb, bool /*allowCachedResult*/)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getTxByHash] invalid state: {}", (int)state_.load());
      return false;
   }
   if (addGetTxCallback(hash, cb)) {
      return true;
   }
   const auto &cbWrap = [this, hash]
      (ReturnMessage<AsyncClient::TxResult> tx)->void
   {
      try {
         auto retTx = tx.get();
         callGetTxCallbacks(hash, retTx);
      }
      catch (const std::exception &e) {
         logger_->error("[getTxByHash (cbUpdateCache)] Return data error - {} "
            "- hash {}", e.what(), hash.toHexStr());
         callGetTxCallbacks(hash, {});
      }
   };
   bdv_->getTxByHash(hash, cbWrap);
   return true;
}

bool ArmoryConnection::getTXsByHash(const std::set<BinaryData> &hashes
   , const TXsCb &cb, bool /*allowCachedResult - ignored here*/)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::getTXsByHash] invalid state: {}", (int)state_.load());
      return false;
   }
   if (hashes.empty()) {
      logger_->warn("[ArmoryConnection::getTXsByHash] empty hash set");
      return false;
   }

   const auto cbWrap = [logger=logger_, cb]
      (ReturnMessage<AsyncClient::TxBatchResult> msg)->void
   {
      try {
         if (cb) {
            cb(std::move(msg.get()), nullptr);
         }
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getTXsByHash] failed to get: {}", e.what());
         if (cb) {
            cb({}, std::make_exception_ptr(e));
         }
      }
   };
   bdv_->getTxBatchByHash(hashes, cbWrap);
   return true;
}

bool ArmoryConnection::getRawHeaderForTxHash(const BinaryData& inHash, const BinaryDataCb &callback)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}",__func__, (int)state_.load());
      return false;
   }

   // For now, don't worry about chaining callbacks or Tx caches. Just dump
   // everything into the BDV. This may need to change in the future, making the
   // call more like getTxByHash().
   const auto &cbWrap = [logger=logger_, callback, inHash](ReturnMessage<BinaryData> bd) {
      try {
         if (callback) {
            callback(bd.get());
         }
      }
      catch (const std::exception &e) {
         logger->error("[ArmoryConnection::getTXsByHash (cbWrap)] Return data error - "
            "{} - hash {}", e.what(), inHash.toHexStr(true));
      }
   };
   bdv_->getRawHeaderForTxHash(inHash, cbWrap);

   return true;
}

bool ArmoryConnection::getHeaderByHeight(const unsigned int inHeight, const BinaryDataCb &callback)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }

   // For now, don't worry about chaining callbacks or Tx caches. Just dump
   // everything into the BDV. This may need to change in the future, making the
   // call more like getTxByHash().
   const auto &cbWrap = [logger=logger_, callback, inHeight](ReturnMessage<BinaryData> bd) {
      try {
         if (callback) {
            callback(bd.get());
         }
      }
      catch (const std::exception &e) {
         logger->error("[getHeaderByHeight (cbWrap)] Return data error - {} - "
            "height {}", e.what(), inHeight);
      }
   };
   bdv_->getHeaderByHeight(inHeight, cbWrap);
   return true;
}

// Frontend for Armory's estimateFee() call. Used to get the "econimical" fee
// that Bitcoin Core estimates for successful insertion into a block within a
// given number (2-1008) of blocks.
bool ArmoryConnection::estimateFee(unsigned int nbBlocks, const FloatCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }
   const auto &cbProcess = [this, cb, nbBlocks](DBClientClasses::FeeEstimateStruct feeStruct) {
      if (feeStruct.error_.empty()) {
         if (cb) {
            cb(feeStruct.val_);
         }
      }
      else {
         logger_->warn("[estimateFee (cbProcess)] error '{}' for nbBlocks={}"
            , feeStruct.error_, nbBlocks);
         if (cb) {
            cb(0);
         }
      }
   };
   const auto &cbWrap = [logger=logger_, cbProcess, cb, nbBlocks]
                        (ReturnMessage<DBClientClasses::FeeEstimateStruct> feeStruct) {
      try {
         cbProcess(feeStruct.get());
      }
      catch (const std::exception &e) {
         logger->error("[estimateFee (cbWrap)] Return data error - {} - {} "
            "blocks", e.what(), nbBlocks);
         if (cb) {
            cb(std::numeric_limits<float>::infinity());
         }
      }
   };
   bdv_->estimateFee(nbBlocks, FEE_STRAT_ECONOMICAL, cbWrap);
   return true;
}

// Frontend for Armory's getFeeSchedule() call. Used to get the range of fees
// that Armory caches. The fees/byte are estimates for what's required to get
// successful insertion of a TX into a block within X number of blocks.
bool ArmoryConnection::getFeeSchedule(const FloatMapCb &cb)
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[{}] invalid state: {}", __func__, (int)state_.load());
      return false;
   }

   const auto &cbProcess = [this, cb] (std::map<unsigned int
         , DBClientClasses::FeeEstimateStruct> feeStructMap) {
      // Create a new map with the # of blocks and the recommended fee/byte.
      std::map<unsigned int, float> feeFloatMap;
      for (auto it : feeStructMap) {
         if (it.second.error_.empty()) {
            feeFloatMap.emplace(it.first, std::move(it.second.val_));
         }
         else {
            logger_->warn("[getFeeSchedule (cbProcess)] error '{}' - {} blocks "
               "- {} sat/byte", it.first, it.second.val_, it.second.error_);
            feeFloatMap.insert(std::pair<unsigned int, float>(it.first, 0.0f));
         }
      }
      if (cb) {
         cb(feeFloatMap);
      }
   };

   const auto &cbWrap = [logger=logger_, cbProcess]
      (ReturnMessage<std::map<unsigned int, DBClientClasses::FeeEstimateStruct>> feeStructMap)
   {
      try {
         cbProcess(feeStructMap.get());
      }
      catch (const std::exception &e) {
         logger->error("[getFeeSchedule (cbProcess)] Return data error - {}"
            , e.what());
      }
   };
   bdv_->getFeeSchedule(FEE_STRAT_ECONOMICAL, cbWrap);
   return true;
}

std::string ArmoryConnection::pushZC(const BinaryData& rawTx) const
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::pushZC] invalid state: {}", (int)state_.load());
      return "";
   }

   return bdv_->broadcastZC(rawTx);
}

std::string ArmoryConnection::pushZCs(const std::vector<BinaryData> &txs) const
{
   if (!bdv_ || (state_ != ArmoryState::Ready)) {
      logger_->error("[ArmoryConnection::pushZCs] invalid state: {}", (int)state_.load());
      return "";
   }
   return bdv_->broadcastZC(txs);
}

unsigned int ArmoryConnection::getConfirmationsNumber(uint32_t blockNum) const
{
   const auto curBlock = topBlock();
   if ((curBlock != UINT32_MAX) && (blockNum < uint32_t(-1))) {
      return curBlock + 1 - blockNum;
   }
   return 0;
}

unsigned int ArmoryConnection::getConfirmationsNumber(const DBClientClasses::LedgerEntry &item) const
{
   return getConfirmationsNumber(item.getBlockNum());
}

bool ArmoryConnection::isTransactionVerified(const DBClientClasses::LedgerEntry &item) const
{
   return isTransactionVerified(item.getBlockNum());
}

bool ArmoryConnection::isTransactionVerified(uint32_t blockNum) const
{
   return getConfirmationsNumber(blockNum) >= 6;
}

bool ArmoryConnection::isTransactionConfirmed(const DBClientClasses::LedgerEntry &item) const
{
   return getConfirmationsNumber(item) > 1;
}

void ArmoryConnection::onRefresh(const std::vector<BinaryData>& ids)
{
   const bool online = (state_ == ArmoryState::Ready);
#ifndef NDEBUG
   if (logger_->level() <= spdlog::level::debug) {
      std::string idString;
      for (const auto &id : ids) {
         idString += id.toBinStr() + " ";
      }
      logger_->debug("[ArmoryConnection::onRefresh] online={} {}[{}]"
         , online, idString, ids.size());
   }
#endif   //NDEBUG
   addToQueue([ids, online](ArmoryCallbackTarget *tgt) {
      tgt->onRefresh(ids, online);
   });
}

void ArmoryConnection::onZCsReceived(const std::string& requestId
   , const std::vector<std::shared_ptr<DBClientClasses::LedgerEntry>> &entries)
{
   const auto newEntries = bs::TXEntry::fromLedgerEntries(entries);

   addToQueue([requestId, newEntries](ArmoryCallbackTarget *tgt) {
      tgt->onZCReceived(requestId, newEntries);
   });
}

void ArmoryConnection::onZCsInvalidated(const std::set<BinaryData> &ids)
{
   addToQueue([ids](ArmoryCallbackTarget *tgt) {
      tgt->onZCInvalidated(ids);
   });
}

std::shared_ptr<AsyncClient::BtcWallet> ArmoryConnection::instantiateWallet(const std::string &walletId)
{
   if (!bdv_ || (state() == ArmoryState::Offline)) {
      logger_->error("[{}] can't instantiate", __func__);
      return nullptr;
   }
   return std::make_shared<AsyncClient::BtcWallet>(bdv_->instantiateWallet(walletId));
}

float ArmoryConnection::toFeePerByte(float fee)
{
   return float(double(fee) * BTCNumericTypes::BalanceDivider / 1000.0);
}

void ArmoryConnection::shutdown()
{
   {
      std::unique_lock<std::mutex> lock(actMutex_);
      maintThreadRunning_ = false;
      actCV_.notify_one();
   }
   stopServiceThreads();

   if (cbRemote_) {
      cbRemote_->resetConnection();
   }

   if (thread_.joinable()) {
      thread_.join();
   }
}

void ArmoryCallback::progress(BDMPhase phase,
   const std::vector<std::string> &walletIdVec, float progress,
   unsigned secondsRem, unsigned progressNumeric)
{
   std::lock_guard<std::mutex> lock(mutex_);

   logger_->debug("[{}] {}, {} wallets, {} ({}), {} seconds remain", __func__
                  , (int)phase, walletIdVec.size(), progress, progressNumeric
                  , secondsRem);
   if (connection_) {
      connection_->addToQueue([phase, progress, secondsRem, progressNumeric]
      (ArmoryCallbackTarget *tgt) {
         tgt->onLoadProgress(phase, progress, secondsRem, progressNumeric);
      });
   }
}

void ArmoryCallback::run(BdmNotification bdmNotif)
{
   std::lock_guard<std::mutex> lock(mutex_);

   if (!connection_) {
      return;
   }

   switch (bdmNotif.action_) {
   case BDMAction_Ready:
      logger_->debug("[ArmoryCallback::run] BDMAction_Ready");
      connection_->setTopBlock(bdmNotif.height_);
      connection_->setState(ArmoryState::Ready);
      break;

   case BDMAction_NewBlock:
      logger_->debug("[ArmoryCallback::run] BDMAction_NewBlock {}", bdmNotif.height_);
      connection_->setTopBlock(bdmNotif.height_);
      connection_->setState(ArmoryState::Ready);
      connection_->addToQueue([height=bdmNotif.height_, branchHgt=bdmNotif.branchHeight_]
         (ArmoryCallbackTarget *tgt)
      {
         tgt->onNewBlock(height, branchHgt);
      });
      break;

   case BDMAction_ZC:
      logger_->debug("[ArmoryCallback::run] BDMAction_ZC: {} entries. Request ID {}"
         , bdmNotif.ledgers_.size(), bdmNotif.requestID_);
      connection_->onZCsReceived(bdmNotif.requestID_, bdmNotif.ledgers_);
      break;

   case BDMAction_InvalidatedZC:
      logger_->debug("[ArmoryCallback::run] BDMAction_InvalidateZC: {} entries"
         , bdmNotif.invalidatedZc_.size());
      connection_->onZCsInvalidated(bdmNotif.invalidatedZc_);
      break;

   case BDMAction_Refresh:
      logger_->debug("[ArmoryCallback::run] BDMAction_Refresh");
      connection_->onRefresh(bdmNotif.ids_);
      break;

   case BDMAction_NodeStatus: {
      const auto nodeStatus = *bdmNotif.nodeStatus_;
      logger_->debug("[ArmoryCallback::run] BDMAction_NodeStatus: status={}, RPC status={}"
         , (int)nodeStatus.state(), (int)nodeStatus.rpcState());
      connection_->addToQueue([nodeStatus](ArmoryCallbackTarget *tgt) {
         tgt->onNodeStatus(nodeStatus);
      });
      break;
   }

   case BDMAction_BDV_Error: {
      const auto bdvError = bdmNotif.error_;
      logger_->debug("[ArmoryCallback::run] BDMAction_BDV_Error {}, str: {}. request ID {}"
         , (int)bdvError.errCode_, bdvError.errorStr_, bdmNotif.requestID_);
      switch (static_cast<ArmoryErrorCodes>(bdvError.errCode_)) {
      case ArmoryErrorCodes::ZcBroadcast_Error:
      case ArmoryErrorCodes::ZcBroadcast_AlreadyInChain:
      case ArmoryErrorCodes::ZcBatch_Timeout:
      case ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool:
      case ArmoryErrorCodes::ZcBroadcast_VerifyRejected:
      case ArmoryErrorCodes::ZcBroadcast_Pending:
         connection_->addToQueue([requestId = bdmNotif.requestID_, bdvError](ArmoryCallbackTarget *tgt) {
            tgt->onTxBroadcastError(requestId, bdvError.errData_, bdvError.errCode_, bdvError.errorStr_);
         });
         break;
      default:
         connection_->addToQueue([bdvError](ArmoryCallbackTarget *tgt) {
            tgt->onError(bdvError.errCode_, bdvError.errorStr_);
         });
         break;
      }
      break;
   }

   default:
      logger_->debug("[ArmoryCallback::run] unknown BDMAction: {}", (int)bdmNotif.action_);
      break;
   }
}

void ArmoryCallback::disconnected()
{
   logger_->debug("[ArmoryCallback::disconnected]");
   std::lock_guard<std::mutex> lock(mutex_);
   if (connection_) {
      connection_->regThreadRunning_ = false;
      if (connection_->state() != ArmoryState::Cancelled) {
         connection_->setState(ArmoryState::Offline);
      }
   }
}

void ArmoryCallback::resetConnection()
{
   std::lock_guard<std::mutex> lock(mutex_);
   connection_ = nullptr;
}

void bs::TXEntry::merge(const bs::TXEntry &other)
{
   walletIds.insert(other.walletIds.cbegin(), other.walletIds.cend());
   value += other.value;
   blockNum = other.blockNum;
   addresses.insert(addresses.end(), other.addresses.cbegin(), other.addresses.cend());
   merged = true;
}

bs::TXEntry bs::TXEntry::fromLedgerEntry(const DBClientClasses::LedgerEntry &entry)
{
   bs::TXEntry result{ entry.getTxHash(), { entry.getID() }, entry.getValue()
      , entry.getBlockNum(), entry.getTxTime(), entry.isOptInRBF()
      , entry.isChainedZC(), false, std::chrono::steady_clock::now(), {}};
   for (const auto &addr : entry.getScrAddrList()) {
      try {
         const auto &scrAddr = bs::Address::fromHash(addr);
         result.addresses.emplace_back(std::move(scrAddr));
      }
      catch (const std::exception &) {}   // Likely an OP_RETURN output
   }
   return result;
}

std::vector<bs::TXEntry> bs::TXEntry::fromLedgerEntries(const std::vector<DBClientClasses::LedgerEntry> &entries)
{
   // Looks like we don't need to merge TXs here (like it was done before).
   // So there would be two TX when two different local wallets are used (with different wallet IDs),
   // but only one for internal TX (if addresses from same wallet are used).
   std::vector<bs::TXEntry> result;
   for (const auto &entry : entries) {
      result.emplace_back(fromLedgerEntry(entry));
   }
   return result;
}

std::vector<bs::TXEntry> bs::TXEntry::fromLedgerEntries(const std::vector<std::shared_ptr<DBClientClasses::LedgerEntry>> &entries)
{
   std::vector<bs::TXEntry> result;
   for (const auto &entry : entries) {
      result.emplace_back(fromLedgerEntry(*entry));
   }
   return result;
}
