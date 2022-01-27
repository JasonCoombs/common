/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SyncWallet.h"
#include <QLocale>
#include <spdlog/spdlog.h>

#include "CheckRecipSigner.h"
#include "CoinSelection.h"
#include "SyncWalletsManager.h"
#include "WalletSignerContainer.h"
#include "WalletUtils.h"

using namespace bs::sync;

Wallet::Wallet(WalletSignerContainer *container, const std::shared_ptr<spdlog::logger> &logger)
   : signContainer_(container), logger_(logger)
{
   balanceData_ = std::make_shared<BalanceData>();
}

Wallet::~Wallet()
{
   act_.reset();

   // Make sure validityFlag_ is marked as destroyed before other members
   validityFlag_.reset();

   {
      std::unique_lock<std::mutex> lock(balThrMutex_);
      balThreadRunning_ = false;
      balThrCV_.notify_one();
   }
   {
      std::unique_lock<std::mutex> lock(balanceData_->cbMutex);
      balanceData_->cbBalances.clear();
      balanceData_->cbTxNs.clear();
   }
}

std::string Wallet::walletIdInt(void) const
{
   /***
   Overload this if your wallet class supports internal chains.
   A wallet object without an internal chain should throw a
   runtime error.
   ***/
   throw std::runtime_error("not supported");
}

void Wallet::synchronize(const std::function<void()> &cbDone)
{
   const auto &cbProcess = [this, cbDone, handle = validityFlag_.handle()]
      (bs::sync::WalletData data) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }

      usedAddresses_.clear();
      for (const auto &addr : data.addresses) {
         addAddress(addr.address, addr.index, false);
         setAddressComment(addr.address, addr.comment, false);
      }

      for (const auto &txComment : data.txComments)
         setTransactionComment(txComment.txHash, txComment.comment, false);

      if (cbDone) {
         cbDone();
      }
   };

   signContainer_->syncWallet(walletId(), cbProcess);
}

std::string Wallet::getAddressComment(const bs::Address &address) const
{
   std::lock_guard<std::mutex> lock(mutex_);
   const auto &itComment = addrComments_.find(address);
   if (itComment != addrComments_.end()) {
      return itComment->second;
   }
   return {};
}

bool Wallet::setAddressComment(const bs::Address &address, const std::string &comment, bool sync)
{
   if (address.empty() || comment.empty()) {
      return false;
   }
   std::lock_guard<std::mutex> lock(mutex_);
   addrComments_[address] = comment;
   if (sync && signContainer_) {
      signContainer_->syncAddressComment(walletId(), address, comment);
   }
   if (wct_ && sync) {
      wct_->addressAdded(walletId());
   }
   return true;
}

std::string Wallet::getTransactionComment(const BinaryData &txHash)
{
   const auto &itComment = txComments_.find(txHash);
   if (itComment != txComments_.end()) {
      return itComment->second;
   }
   return {};
}

bool Wallet::setTransactionComment(const BinaryData &txOrHash, const std::string &comment, bool sync)
{
   if (txOrHash.empty() || comment.empty()) {
      return false;
   }
   BinaryData txHash;
   if (txOrHash.getSize() == 32) {
      txHash = txOrHash;
   } else {   // raw transaction then
      Tx tx(txOrHash);
      if (!tx.isInitialized()) {
         return false;
      }
      txHash = tx.getThisHash();
   }
   txComments_[txHash] = comment;
   if (sync && signContainer_) {
      signContainer_->syncTxComment(walletId(), txHash, comment);
   }
   return true;   //stub
}

bool Wallet::isBalanceAvailable() const
{
   return armorySet_.load() && (armory_->state() == ArmoryState::Ready)
      // Keep balances if registration is just updating
      && (isRegistered() == Registered::Registered || isRegistered() == Registered::Updating);
}

BTCNumericTypes::balance_type Wallet::getSpendableBalance() const
{
   if (!isBalanceAvailable()) {
      return std::numeric_limits<double>::infinity();
   }
   return balanceData_->spendableBalance;
}

BTCNumericTypes::balance_type Wallet::getUnconfirmedBalance() const
{
   if (!isBalanceAvailable()) {
      return 0;
   }
   return balanceData_->unconfirmedBalance;
}

BTCNumericTypes::balance_type Wallet::getTotalBalance() const
{
   if (!isBalanceAvailable()) {
      return std::numeric_limits<double>::infinity();
   }
   return balanceData_->totalBalance;
}

std::vector<uint64_t> Wallet::getAddrBalance(const bs::Address &addr) const
{
   if (!isBalanceAvailable()) {
      SPDLOG_LOGGER_ERROR(logger_, "balance is not available for wallet {}", walletId());
      return {};
   }
   std::unique_lock<std::mutex> lock(balanceData_->addrMapsMtx);

   auto iter = balanceData_->addressBalanceMap.find(addr.prefixed());
   if (iter == balanceData_->addressBalanceMap.end()) {
      return {};
   }

   return iter->second;
}

uint64_t Wallet::getAddrTxN(const bs::Address &addr) const
{
   if (!isBalanceAvailable()) {
      SPDLOG_LOGGER_ERROR(logger_, "balance is not available for wallet {}", walletId());
      return {};
   }
   std::unique_lock<std::mutex> lock(balanceData_->addrMapsMtx);

   auto iter = balanceData_->addressTxNMap.find(addr.prefixed());
   if (iter == balanceData_->addressTxNMap.end()) {
      return 0;
   }

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
////
//// Combined DB fetch methods
////
////////////////////////////////////////////////////////////////////////////////

bool Wallet::updateBalances(const std::function<void(void)> &cb)
{  /***
   The callback is only used to signify request completion, use the
   get methods to grab the individual balances
   ***/
   if (!armory_) {
      std::cout << "no armory\n";
      return false;
   }

   size_t cbSize = 0;
   {
      std::unique_lock<std::mutex> lock(balanceData_->cbMutex);
      cbSize = balanceData_->cbBalances.size();
      balanceData_->cbBalances.push_back(cb);
   }
   if (cbSize == 0) {
      std::vector<std::string> walletIDs;
      walletIDs.push_back(walletId());
      try {
         walletIDs.push_back(walletIdInt());
      } catch (std::exception&) {}

      const auto onCombinedBalances = [balanceData = balanceData_, walletId=walletId()]
         (const std::map<std::string, CombinedBalances> &balanceMap)
      {
         BTCNumericTypes::balance_type total = 0, spendable = 0, unconfirmed = 0;
         uint64_t addrCount = 0;
         {
            std::unique_lock<std::mutex> lock(balanceData->addrMapsMtx);
            for (const auto &wltBal : balanceMap) {
               total += static_cast<BTCNumericTypes::balance_type>(
                  wltBal.second.walletBalanceAndCount_[0]) / BTCNumericTypes::BalanceDivider;
               /*      spendable += static_cast<BTCNumericTypes::balance_type>(
                        wltBal.second.walletBalanceAndCount_[1]) / BTCNumericTypes::BalanceDivider;*/
               unconfirmed += static_cast<BTCNumericTypes::balance_type>(
                  wltBal.second.walletBalanceAndCount_[2]) / BTCNumericTypes::BalanceDivider;

               //wallet txn count
               addrCount += wltBal.second.walletBalanceAndCount_[3];

               //address balances
               updateMap<std::map<BinaryData, std::vector<uint64_t>>>(
                  wltBal.second.addressBalances_, balanceData->addressBalanceMap);
            }
            spendable = total - unconfirmed;
         }

         balanceData->totalBalance = total;
         balanceData->spendableBalance = spendable;
         balanceData->unconfirmedBalance = unconfirmed;
         balanceData->addrCount = addrCount;

         std::vector<std::function<void(void)>> cbCopy;
         {
            std::unique_lock<std::mutex> lock(balanceData->cbMutex);
            cbCopy.swap(balanceData->cbBalances);
         }
         for (const auto &cb : cbCopy) {
            if (cb) {
               cb();
            }
         }
      };
      return armory_->getCombinedBalances(walletIDs, onCombinedBalances);
   } else {          // if the callbacks queue is not empty, don't call
      return true;   // armory's RPC - just add the callback and return
   }
}

bool Wallet::getSpendableTxOutList(const ArmoryConnection::UTXOsCb &cb, uint64_t val, bool excludeReservation)
{   //combined utxo fetch method

   if (!isBalanceAvailable()) {
      return false;
   }

   const auto &cbTxOutList = [this, val, cb, handle = validityFlag_.handle(), excludeReservation]
      (const std::vector<UTXO> &txOutList) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }
      std::vector<UTXO> txOutListCopy = txOutList;
      if (UtxoReservation::instance() && excludeReservation) {
         UtxoReservation::instance()->filter(txOutListCopy, reservedUTXOs_);
      }
      cb(bs::selectUtxoForAmount(std::move(txOutListCopy), val));
   };

   std::vector<std::string> walletIDs;
   walletIDs.push_back(walletId());
   try {
      walletIDs.push_back(walletIdInt());
   }
   catch(std::exception&)
   {}

   return armory_->getSpendableTxOutListForValue(walletIDs, std::numeric_limits<uint64_t>::max(), cbTxOutList);
}

bool Wallet::getSpendableZCList(const ArmoryConnection::UTXOsCb &cb) const
{
   if (!isBalanceAvailable()) {
      return false;
   }

   std::vector<std::string> walletIDs;
   walletIDs.push_back(walletId());
   try {
      walletIDs.push_back(walletIdInt());
   }
   catch (std::exception&)
   {}

   armory_->getSpendableZCoutputs(walletIDs, cb);
   return true;
}

bool Wallet::getRBFTxOutList(const ArmoryConnection::UTXOsCb &cb) const
{
   if (!isBalanceAvailable()) {
      return false;
   }

   std::vector<std::string> walletIDs;
   walletIDs.push_back(walletId());
   try {
      walletIDs.push_back(walletIdInt());
   }
   catch (std::exception&)
   {}

   armory_->getRBFoutputs(walletIDs, cb);
   return true;
}

std::vector<UTXO> Wallet::getIncompleteUTXOs() const
{
   return reservedUTXOs_;
}

void Wallet::setWCT(WalletCallbackTarget *wct)
{
   wct_ = wct;
}

bool Wallet::getAddressTxnCounts(const std::function<void(void)> &cb)
{  /***
   Same as updateBalances, this methods grabs the addr txn count
   for all addresses in wallet (inner chain included) and caches
   them locally.

   Use getAddressTxnCount to get a specific count for a given
   address from the cache.
   ***/
   if (!armory_) {
      return false;
   }
   size_t cbSize = 0;
   {
      std::unique_lock<std::mutex> lock(balanceData_->cbMutex);
      cbSize = balanceData_->cbTxNs.size();
      balanceData_->cbTxNs.push_back(cb);
   }
   if (cbSize == 0) {
      std::vector<std::string> walletIDs;
      walletIDs.push_back(walletId());
      try {
         walletIDs.push_back(walletIdInt());
      } catch (std::exception&) {}

      const auto &cbTxNs = [balanceData = balanceData_]
         (const std::map<std::string, CombinedCounts> &countMap)
      {
         for (const auto &count : countMap) {
            std::unique_lock<std::mutex> lock(balanceData->addrMapsMtx);
            updateMap<std::map<BinaryData, uint64_t>>(
               count.second.addressTxnCounts_, balanceData->addressTxNMap);
         }

         auto cbTxNsCopy = std::make_shared<std::vector<std::function<void(void)>>>();
         {
            std::unique_lock<std::mutex> lock(balanceData->cbMutex);
            cbTxNsCopy->swap(balanceData->cbTxNs);
         }
         for (const auto &cb : *cbTxNsCopy) {
            if (cb) {
               cb();
            }
         }
      };
      return armory_->getCombinedTxNs(walletIDs, cbTxNs);
   }
   else {
      return true;
   }
}

////////////////////////////////////////////////////////////////////////////////

bool Wallet::getHistoryPage(const std::shared_ptr<AsyncClient::BtcWallet> &btcWallet
   , uint32_t id, std::function<void(const Wallet *wallet
   , std::vector<DBClientClasses::LedgerEntry>)> clientCb, bool onlyNew) const
{
   if (!isBalanceAvailable()) {
      return false;
   }
   const auto &cb = [this, id, onlyNew, clientCb, handle = validityFlag_.handle(), logger=logger_]
                    (ReturnMessage<std::vector<DBClientClasses::LedgerEntry>> entries) mutable -> void
   {
      try {
         auto le = entries.get();

         ValidityGuard lock(handle);
         if (!handle.isValid()) {
            return;
         }
         if (!onlyNew) {
            clientCb(this, le);
         }
         else {
            const auto &histPage = historyCache_.find(id);
            if (histPage == historyCache_.end()) {
               clientCb(this, le);
            }
            else if (histPage->second.size() == le.size()) {
               clientCb(this, {});
            }
            else {
               std::vector<DBClientClasses::LedgerEntry> diff;
               struct comparator {
                  bool operator() (const DBClientClasses::LedgerEntry &a, const DBClientClasses::LedgerEntry &b) const {
                     return (a.getTxHash() < b.getTxHash());
                  }
               };
               std::set<DBClientClasses::LedgerEntry, comparator> diffSet;
               diffSet.insert(le.begin(), le.end());
               for (const auto &entry : histPage->second) {
                  diffSet.erase(entry);
               }
               for (const auto &diffEntry : diffSet) {
                  diff.emplace_back(diffEntry);
               }
               clientCb(this, diff);
            }
         }
         historyCache_[id] = le;
      }
      catch (const std::exception& e) {
         if (logger != nullptr) {
            logger->error("[bs::sync::Wallet::getHistoryPage] Return data " \
               "error - {} - ID {}", e.what(), id);
         }
      }
   };
   btcWallet->getHistoryPage(id, cb);
   return true;
}

QString Wallet::displayTxValue(int64_t val) const
{
   return QLocale().toString(val / BTCNumericTypes::BalanceDivider, 'f', BTCNumericTypes::default_precision);
}

void Wallet::setArmory(const std::shared_ptr<ArmoryConnection> &armory)
{
   if (!armory_ && (armory != nullptr)) {
      armory_ = armory;
      armorySet_ = true;

      /*
      Do not set callback target if it is already initialized. This
      allows for unit tests to set a custom ACT.
      */
      if (act_ == nullptr) {
         act_ = make_unique<WalletACT>(this);
         act_->init(armory_.get());
      }
   }
}

void Wallet::onZCInvalidated(const std::set<BinaryData> &ids)
{
   unsigned int processedEntries = 0;
   for (const auto &id : ids) {
      const auto &itTx = zcEntries_.find(id);
      if (itTx == zcEntries_.end()) {
         continue;
      }
      BTCNumericTypes::balance_type invalidatedBalance = 0;
      for (size_t i = 0; i < itTx->second.getNumTxOut(); ++i) {
         const auto txOut = itTx->second.getTxOutCopy(i);
         const auto txType = txOut.getScriptType();
         if (txType == TXOUT_SCRIPT_OPRETURN || txType == TXOUT_SCRIPT_NONSTANDARD) {
            continue;
         }
         const auto addr = bs::Address::fromTxOut(txOut);
         if (containsAddress(addr)) {
            const auto addrBal = txOut.getValue();
            invalidatedBalance += addrBal / BTCNumericTypes::BalanceDivider;

            std::unique_lock<std::mutex> lock(balanceData_->addrMapsMtx);
            auto it = balanceData_->addressBalanceMap.find(addr.prefixed());
            if (it == balanceData_->addressBalanceMap.end()) {
               return;
            }

            auto &addrBalances = it->second;
            if (addrBalances.size() < 3) {
               SPDLOG_LOGGER_ERROR(logger_, "invalid addr balances vector");
               return;
            }

            addrBalances[0] -= addrBal;
            addrBalances[1] -= addrBal;
         }
      }
      balanceData_->unconfirmedBalance = balanceData_->unconfirmedBalance - invalidatedBalance;
      logger_->debug("[{}] {} processed invalidated ZC entry {}, balance: {}"
         , __func__, walletId(), itTx->first.toHexStr(true), invalidatedBalance);
      zcEntries_.erase(itTx);
      processedEntries++;
   }
   if (processedEntries && wct_) {
      wct_->balanceUpdated(walletId());
   }
}

void Wallet::onZeroConfReceived(const std::vector<bs::TXEntry> &entries)
{
   if (skipPostOnline_) {
      return;
   }

   const auto &cbTX = [this, balanceData = balanceData_, handle = validityFlag_.handle(), armory=armory_]
      (const Tx &tx) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }

      for (size_t i = 0; i < tx.getNumTxOut(); ++i) {
         const auto txOut = tx.getTxOutCopy(i);
         const auto txType = txOut.getScriptType();
         if (txType == TXOUT_SCRIPT_OPRETURN || txType == TXOUT_SCRIPT_NONSTANDARD) {
            continue;
         }
         const auto addr = bs::Address::fromTxOut(txOut);
         if (containsAddress(addr)) {
            zcEntries_[tx.getThisHash()] = tx;
            break;
         }
      }

      for (size_t i = 0; i < tx.getNumTxIn(); ++i) {
         const TxIn in = tx.getTxInCopy(i);
         const OutPoint op = in.getOutPoint();

         const auto &cbPrevTX = [this, balanceData, idx=op.getTxOutIndex(), handle](const Tx &prevTx) mutable
         {
            if (!prevTx.isInitialized()) {
               return;
            }
            const TxOut prevOut = prevTx.getTxOutCopy(idx);
            const auto addr = bs::Address::fromTxOut(prevOut);
            if (!containsAddress(addr)) {
               return;
            }
            bool updated = false;
            ValidityGuard lock(handle);
            if (!handle.isValid()) {
               return;
            }
            {
               std::unique_lock<std::mutex> lock(balanceData->addrMapsMtx);
               const auto &itTxn = balanceData->addressTxNMap.find(addr.id());
               if (itTxn != balanceData->addressTxNMap.end()) {
                  itTxn->second++;
                  updated = true;
               }
            }
            if (updated && wct_) {
               wct_->balanceUpdated(walletId());
            }
         };
         armory->getTxByHash(op.getTxHash(), cbPrevTX, true);
      }
   };
   for (const auto &entry : entries) {
      armory_->getTxByHash(entry.txHash, cbTX, true);
   }
   updateBalances([this, handle = validityFlag_.handle(), logger=logger_]() mutable {    // TxNs are not updated for ZCs
      ValidityGuard lock(handle);
      if (!trackLiveAddresses_ || !handle.isValid()) {
         return;
      }
      trackChainAddressUse([this, handle, logger](bs::sync::SyncState st) mutable {
         logger->debug("{}: new live address found: {}", walletId(), (int)st);
         if (st == bs::sync::SyncState::Success) {
            ValidityGuard lock(handle);
            if (!handle.isValid()) {
               return;
            }
            synchronize([this, handle]() mutable {
               ValidityGuard lock(handle);
               if (!handle.isValid()) {
                  return;
               }
               logger_->debug("[Wallet::onZeroConfReceived] synchronized after addresses are tracked");
               if (wct_) {
                  wct_->addressAdded(walletId());
               }
            });
         }
      });
   });
}

void Wallet::onNewBlock(unsigned int, unsigned int)
{
   if (!skipPostOnline_) {
      init(true);
   }
}

void Wallet::onBalanceAvailable(const std::function<void()> &cb) const
{
   if (isBalanceAvailable()) {
      if (cb) {
         cb();
      }
      return;
   }

   const auto thrBalAvail = [this, cb, handle = validityFlag_.handle()]() mutable
   {
      while (balThreadRunning_) {
         {
            ValidityGuard lock(handle);
            if (!handle.isValid()) {
               return;
            }
         }
         std::unique_lock<std::mutex> lock(balThrMutex_);
         balThrCV_.wait_for(lock, std::chrono::milliseconds{ 100 });
         if (!balThreadRunning_) {
            return;
         }
         if (isBalanceAvailable()) {
            for (const auto &cb : cbBalThread_) {
               if (cb) {
                  cb();
               }
            }
            cbBalThread_.clear();
            balThreadRunning_ = false;
            return;
         }
      }
   };
   {
      std::unique_lock<std::mutex> lock(balThrMutex_);
      cbBalThread_.emplace_back(std::move(cb));
   }
   if (!balThreadRunning_) {
      balThreadRunning_ = true;
      std::thread(thrBalAvail).detach();
   }
}

void Wallet::onRefresh(const std::vector<BinaryData> &ids, bool online)
{
   for (const auto &id : ids) {
      if (id == BinaryData::fromString(regId_)) {
         regId_.clear();
         logger_->debug("[bs::sync::Wallet::registerWallet] wallet {} registered", walletId());
         isRegistered_ = Registered::Registered;
         init();

         const auto &cbTrackAddrChain = [this, handle = validityFlag_.handle()]
            (bs::sync::SyncState st) mutable
         {
            ValidityGuard lock(handle);
            if (!handle.isValid()) {
               return;
            }
            if (wct_) {
               wct_->walletReady(walletId());
            }
         };
         bs::sync::Wallet::init();
         getAddressTxnCounts([this, cbTrackAddrChain, handle = validityFlag_.handle()]() mutable {
            ValidityGuard lock(handle);
            if (!handle.isValid()) {
               return;
            }
            trackChainAddressUse(cbTrackAddrChain);
         });
      }
   }
}

void Wallet::onRegistered()
{
   init();
}

#if 0
std::vector<std::string> Wallet::registerWallet(const std::shared_ptr<ArmoryConnection> &armory, bool asNew)
{
   setArmory(armory);

   if (armory_) {
      const auto wallet = armory_->instantiateWallet(walletId());
      const auto &addrHashes = getAddrHashes();
      regId_ = wallet->registerAddresses(addrHashes, asNew);
      registeredAddresses_.insert(addrHashes.begin(), addrHashes.end());
      logger_->debug("[bs::sync::Wallet::registerWallet] register wallet {}, {} addresses = {}"
         , walletId(), getAddrHashes().size(), regId_);
      return { regId_ };
   }
   else {
      logger_->error("[bs::sync::Wallet::registerWallet] no armory");
   }
   return {};
}

void Wallet::unregisterWallet()
{
   historyCache_.clear();
}
#endif   //0

Wallet::UnconfTgtData Wallet::unconfTargets() const
{
   return { { walletId(), 1 } };
}

Wallet::WalletRegData Wallet::regData() const
{
   WalletRegData result;
   const auto& addrHashes = getAddrHashes();
   result[walletId()] = addrHashes;
   registeredAddresses_.insert(addrHashes.begin(), addrHashes.end());
   logger_->debug("[bs::sync::Wallet::regData] wallet {}, {} addresses = {}"
      , walletId(), getAddrHashes().size(), regId_);
   return result;
}

void Wallet::init(bool force)
{
   if (!firstInit_ || force) {
      auto cbCounter = std::make_shared<int>(2);
      const auto &cbBalTxN = [this, cbCounter, handle = validityFlag_.handle()]() mutable {
         ValidityGuard lock(handle);
         if (!handle.isValid()) {
            return;
         }
         (*cbCounter)--;
         if ((*cbCounter <= 0)) {
            if (wct_) {
               wct_->balanceUpdated(walletId());
            }
         }
      };
      updateBalances(cbBalTxN);
      getAddressTxnCounts(cbBalTxN);
      firstInit_ = true;
   }
}

bs::core::wallet::TXSignRequest wallet::createTXRequest(const std::vector<std::string> &walletsIds
   , const std::vector<UTXO> &inputs
   , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &recipients
   , bool allowBroadcasts, const bs::Address &changeAddr
   , const std::string &changeIndex
   , const uint64_t fee
   , bool isRBF)
{
   bs::core::wallet::TXSignRequest request;
   request.walletIds = walletsIds;
   uint64_t inputAmount = 0;
   uint64_t spendAmount = 0;

   if (inputs.empty()) {
      throw std::logic_error("no UTXOs");
   }

   for (const auto& utxo : inputs) {
      auto spender = std::make_shared<Armory::Signer::ScriptSpender>(utxo);
      if (isRBF) {
         spender->setSequence(UINT32_MAX - 2);
      }
      request.armorySigner_.addSpender(spender);
      inputAmount += utxo.getValue();
   }

   for (const auto& recipient : recipients) {
      if (recipient == nullptr) {
         throw std::logic_error("invalid recipient");
      }
      spendAmount += recipient->getValue();
      request.armorySigner_.addRecipient(recipient);
   }
   if (inputAmount < spendAmount + fee) {
      throw std::logic_error(fmt::format("input amount {} is less than spend + fee ({})", inputAmount, spendAmount + fee));
   }

   request.RBF = isRBF;
   request.fee = fee;

   const bs::XBTAmount::satoshi_type changeAmount = inputAmount - (spendAmount + fee);
   if (changeAmount) {
      if (changeAddr.empty()) {
         throw std::logic_error("can't get change address for " + std::to_string(changeAmount));
      }

      request.change.value = changeAmount;
      request.change.address = changeAddr;
      request.change.index = changeIndex;

      auto changeRecip = changeAddr.getRecipient(bs::XBTAmount(changeAmount));
      request.armorySigner_.addRecipient(changeRecip);
   }

   request.allowBroadcasts = allowBroadcasts;

   return request;
}

bs::core::wallet::TXSignRequest wallet::createTXRequest(const std::vector<Wallet*> &wallets
   , const std::vector<UTXO> &inputs
   , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &recipients
   , bool allowBroadcasts
   , const bs::Address &changeAddr
   , const uint64_t fee, bool isRBF)
{
   std::vector<std::string> walletIds;
   for (const auto &wallet : wallets) {
      walletIds.push_back(wallet->walletId());
   }

   std::string changeIndex;
   if (changeAddr.isValid()) {
      for (const auto &wallet : wallets) {
         auto index = wallet->getAddressIndex(changeAddr);
         if (!index.empty()) {
            changeIndex = index;
            break;
         }
      }
      if (changeIndex.empty()) {
         throw std::logic_error("can't find change address index");
      }
   }

   return createTXRequest(walletIds, inputs, recipients, allowBroadcasts, changeAddr, changeIndex, fee, isRBF);
}

bs::core::wallet::TXSignRequest wallet::createTXRequest(const std::vector<std::shared_ptr<Wallet>> &wallets
   , const std::vector<UTXO> &inputs
   , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &recipients
   , bool allowBroadcasts, const bs::Address &changeAddr
   , const uint64_t fee, bool isRBF)
{
   std::vector<Wallet*> walletsCopy;
   for (const auto &wallet : wallets) {
      walletsCopy.push_back(wallet.get());
   }
   return createTXRequest(walletsCopy, inputs, recipients, allowBroadcasts, changeAddr, fee, isRBF);
}

bs::core::wallet::TXSignRequest Wallet::createTXRequest(const std::vector<UTXO> &inputs
   , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &recipients, bool allowBroadcasts, const uint64_t fee
   , bool isRBF, const bs::Address &changeAddress)
{
   if (!changeAddress.empty()) {
      setAddressComment(changeAddress, wallet::Comment::toString(wallet::Comment::ChangeAddress));
   }
   return wallet::createTXRequest({ this }, inputs, recipients, allowBroadcasts, changeAddress, fee, isRBF);
}

bs::core::wallet::TXSignRequest Wallet::createPartialTXRequest(uint64_t spendVal
   , const std::vector<UTXO> &inputs
   , std::pair<bs::Address, unsigned> changePair
   , float feePerByte
   , const RecipientMap &recipients
   , const Codec_SignerState::SignerState &prevPart
   , unsigned assumedRecipientCount)
{
   std::map<UTXO, std::string> inputsCopy;
   for (const auto &input : inputs) {
      inputsCopy[input] = walletId();
   }
   return WalletsManager::createPartialTXRequest(
      spendVal, inputsCopy, changePair.first, feePerByte
      , armory_->topBlock(), recipients, changePair.second
      , prevPart, false, assumedRecipientCount, logger_);
}

void WalletACT::onLedgerForAddress(const bs::Address &addr
   , const std::shared_ptr<AsyncClient::LedgerDelegate> &ld)
{
   std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)> cb = nullptr;
   {
      std::unique_lock<std::mutex> lock(parent_->balanceData_->cbMutex);
      const auto &itCb = parent_->cbLedgerByAddr_.find(addr);
      if (itCb == parent_->cbLedgerByAddr_.end()) {
         return;
      }
      cb = itCb->second;
      parent_->cbLedgerByAddr_.erase(itCb);
   }
   if (cb) {
      cb(ld);
   }
}

#if 0
bool Wallet::getLedgerDelegateForAddress(const bs::Address &addr
   , const std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)> &cb)
{
   if (!armory_) {
      return false;
   }
   {
      std::unique_lock<std::mutex> lock(balanceData_->cbMutex);
      const auto &itCb = cbLedgerByAddr_.find(addr);
      if (itCb != cbLedgerByAddr_.end()) {
         logger_->error("[bs::sync::Wallet::getLedgerDelegateForAddress] ledger callback for addr {} already exists", addr.display());
         return false;
      }
      cbLedgerByAddr_[addr] = cb;
   }
   return armory_->getLedgerDelegateForAddress(walletId(), addr);
}
#endif   //0

int Wallet::addAddress(const bs::Address &addr, const std::string &index
   , bool sync)
{
   if (!addr.empty()) {
      usedAddresses_.push_back(addr);
   }

   if (sync && signContainer_) {
      std::string idxCopy = index;
      if (idxCopy.empty() && !addr.empty()) {
         idxCopy = getAddressIndex(addr);
         if (idxCopy.empty()) {
            idxCopy = addr.display();
         }
      }
      signContainer_->syncNewAddress(walletId(), idxCopy, nullptr);
   }

   return (usedAddresses_.size() - 1);
}

#if 0
void Wallet::syncAddresses()
{
   if (armory_) {
      registerWallet();
   }

   if (signContainer_) {
      std::set<BinaryData> addrSet;
      for (const auto &addr : getUsedAddressList()) {
         addrSet.insert(addr.id());
      }
      signContainer_->syncAddressBatch(walletId(), addrSet, [](bs::sync::SyncState) {});
   }
}
#endif   //0

void Wallet::newAddresses(const std::vector<std::string> &inData
   , const CbAddresses &cb)
{
   if (signContainer_) {
      signContainer_->syncNewAddresses(walletId(), inData, cb);
   } else {
      if (logger_) {
         logger_->error("[bs::sync::Wallet::newAddresses] no signer set");
      }
   }
}

void Wallet::trackChainAddressUse(const std::function<void(bs::sync::SyncState)> &cb)
{
   if (!signContainer_) {
      cb(bs::sync::SyncState::NothingToDo);
      return;
   }
   //1) round up all addresses that have a tx count
   std::set<BinaryData> usedAddrSet;
   for (auto& addrPair : balanceData_->addressTxNMap) {
      if (addrPair.second != 0) {
         usedAddrSet.insert(addrPair.first);
      }
   }

   {
      std::unique_lock<std::mutex> lock(balanceData_->addrMapsMtx);
      for (auto& addrPair : balanceData_->addressBalanceMap) {
         if (usedAddrSet.find(addrPair.first) != usedAddrSet.end()) {
            continue;   // skip already added addresses
         }
         if (!addrPair.second.empty()) {
            bool hasBalance = false;
            for (int i = 0; i < 3; ++i) {
               if (addrPair.second[i] > 0) {
                  hasBalance = true;
                  break;
               }
            }
            if (hasBalance) {
               usedAddrSet.insert(addrPair.first);
            }
         }
      }
   }

   // Workaround for case when wallet removed and added again without restart
   // and ArmoryDB reports details for old addresses.
   // Could be safely removed when unregister wallet method is added for ArmoryDB.
   std::set<BinaryData> usedAndRegAddrs;
   std::set_intersection(registeredAddresses_.begin(), registeredAddresses_.end()
      , usedAddrSet.begin(), usedAddrSet.end(), std::inserter(usedAndRegAddrs, usedAndRegAddrs.end()));

   logger_->debug("[bs::sync::Wallet::trackChainAddressUse] {}: {} used address[es]", walletId(), usedAndRegAddrs.size());
   //2) send to armory wallet for processing
   signContainer_->syncAddressBatch(walletId(), usedAndRegAddrs, cb);
}

size_t Wallet::getActiveAddressCount()
{
   std::unique_lock<std::mutex> lock(balanceData_->addrMapsMtx);

   size_t count = 0;
   for (auto& addrBal : balanceData_->addressBalanceMap) {
      if (addrBal.second[0] != 0) {
         ++count;
      }
   }
   return count;
}
