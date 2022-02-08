/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SyncWalletsManager.h"

#include "ApplicationSettings.h"
#include "CheckRecipSigner.h"
#include "CoinSelection.h"
#include "FastLock.h"
#include "HeadlessContainer.h"
#include "SyncHDWallet.h"

#include <QCoreApplication>
#include <QDir>
#include <QMutexLocker>

#include <spdlog/spdlog.h>

using namespace Armory::Signer;
using namespace bs::sync;
using namespace bs::signer;

bool isCCNameCorrect(const std::string& ccName)
{
   if ((ccName.length() == 1) && (ccName[0] >= '0') && (ccName[0] <= '9')) {
      return false;
   }

   return true;
}

bs::sync::WalletsManager::WalletsManager(const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<ApplicationSettings>& appSettings
   , const std::shared_ptr<ArmoryConnection> &armory)
   : QObject(nullptr)
   , logger_(logger)
   , appSettings_(appSettings)
   , armoryPtr_(armory)
{
   init(armory.get());

   //ccResolver_ = std::make_shared<CCResolver>();
   threadRunning_ = true;
   thread_ = std::thread(&WalletsManager::threadFunction, this);
}

bs::sync::WalletsManager::~WalletsManager() noexcept
{
   validityFlag_.reset();

   for (const auto &hdWallet : hdWallets_) {
      hdWallet->setWCT(nullptr);
   }
   {
      std::unique_lock<std::mutex> lock(mutex_);
      threadRunning_ = false;
      queueCv_.notify_one();
   }
   if (thread_.joinable()) {
      thread_.join();
   }

   cleanup();
}

void bs::sync::WalletsManager::setSignContainer(const std::shared_ptr<WalletSignerContainer> &container)
{
   signContainer_ = container;
   const auto hct = dynamic_cast<QtHCT*>(container->cbTarget());
   if (hct) {
//      connect(hct, &QtHCT::AuthLeafAdded, this, &WalletsManager::onAuthLeafAdded);
      connect(hct, &QtHCT::walletsListUpdated, this, &WalletsManager::onWalletsListUpdated);
//      connect(hct, &QtHCT::SignerCallbackTarget, this, &WalletsManager::onWalletsListUpdated);
   }
}

void bs::sync::WalletsManager::reset()
{
   QMutexLocker lock(&mtxWallets_);
   wallets_.clear();
   hdWallets_.clear();
   walletNames_.clear();
   readyWallets_.clear();
   isReady_ = false;

   emit walletChanged("");
}

void bs::sync::WalletsManager::syncWallet(const bs::sync::WalletInfo &info, const std::function<void()> &cbDone)
{
   logger_->debug("[WalletsManager::syncWallets] syncing wallet {} ({} {})"
      , *info.ids.cbegin(), info.name, (int)info.format);

   switch (info.format) {
   case bs::sync::WalletFormat::HD:
   {
      try {
         const auto hdWallet = std::make_shared<hd::Wallet>(info, signContainer_.get(), logger_);
         hdWallet->setWCT(this);

         if (hdWallet) {
            const auto &cbHDWalletDone = [this, hdWallet, cbDone] {
               logger_->debug("[WalletsManager::syncWallets] synced HD wallet {}"
                  , hdWallet->walletId());
               saveWallet(hdWallet);
               cbDone();
            };
            hdWallet->synchronize(cbHDWalletDone);
         }
      } catch (const std::exception &e) {
         logger_->error("[WalletsManager::syncWallets] failed to create HD wallet "
            "{}: {}", *info.ids.cbegin(), e.what());
         cbDone();
      }
      break;
   }

   case bs::sync::WalletFormat::Settlement:
      throw std::runtime_error("not implemented");
      break;

   default:
      cbDone();
      logger_->info("[WalletsManager::syncWallets] wallet format {} is not "
         "supported yet", (int)info.format);
      break;
   }
}

bool bs::sync::WalletsManager::syncWallets(const CbProgress &cb)
{
   if (syncState_ == WalletsSyncState::Running) {
      return false;
   }

   const auto &cbWalletInfo = [this, cb](const std::vector<bs::sync::WalletInfo> &wi) {
      auto walletIds = std::make_shared<std::unordered_set<std::string>>();
      for (const auto &info : wi) {
         walletIds->insert(*info.ids.cbegin());
      }
      for (const auto &info : wi) {
         const auto &cbDone = [this, walletIds, id = *info.ids.cbegin(), total = wi.size(), cb]
         {
            walletIds->erase(id);
            if (cb)
               cb(total - walletIds->size(), total);

            if (walletIds->empty()) {
               logger_->debug("[WalletsManager::syncWallets] all wallets synchronized");
               emit walletsSynchronized();
               emit walletChanged("");
               syncState_ = WalletsSyncState::Synced;
            }
         };

         syncWallet(info, cbDone);
      }

      logger_->debug("[WalletsManager::syncWallets] initial wallets synchronized");
      if (wi.empty()) {
         emit walletDeleted("");
      }

      if (wi.empty()) {
         syncState_ = WalletsSyncState::Synced;
         emit walletsSynchronized();
      }
   };

   syncState_ = WalletsSyncState::Running;
   emit walletsSynchronizationStarted();
   if (!signContainer_) {
      logger_->error("[WalletsManager::{}] signer is not set - aborting"
         , __func__);
      return false;
   }
   signContainer_->syncWalletInfo(cbWalletInfo);
   return true;
}

bool bs::sync::WalletsManager::isSynchronising() const
{
   return syncState_ == WalletsSyncState::Running;
}

bool bs::sync::WalletsManager::isWalletsReady() const
{
   if (syncState_ == WalletsSyncState::Synced  && hdWallets_.empty()) {
      return true;
   }
   return isReady_;
}

bool bs::sync::WalletsManager::isReadyForTrading() const
{
   return hasPrimaryWallet();
}

void bs::sync::WalletsManager::saveWallet(const WalletPtr &newWallet)
{
/*   if (hdDummyWallet_ == nullptr) {
      hdDummyWallet_ = std::make_shared<hd::DummyWallet>(logger_);
      hdWalletsId_.insert(hdDummyWallet_->walletId());
      hdWallets_[hdDummyWallet_->walletId()] = hdDummyWallet_;
   }*/
   addWallet(newWallet);
}

void bs::sync::WalletsManager::addWallet(const WalletPtr &wallet, bool isHDLeaf)
{
/*   if (!isHDLeaf && hdDummyWallet_)
      hdDummyWallet_->add(wallet);
   auto ccLeaf = std::dynamic_pointer_cast<bs::sync::hd::CCLeaf>(wallet);
   if (ccLeaf) {
      ccLeaf->setCCDataResolver(ccResolver_);
      updateTracker(ccLeaf);
   }
   wallet->setUserId(userId_);
*/

   {
      QMutexLocker lock(&mtxWallets_);
      const auto &itWallet = wallets_.find(wallet->walletId());
      if (itWallet != wallets_.end()) {
         itWallet->second->merge(wallet);
      }
      else {
         wallets_[wallet->walletId()] = wallet;
      }
   }

   if (isHDLeaf && (wallet->type() == bs::core::wallet::Type::Authentication)) {
      logger_->debug("[WalletsManager] auth leaf changed/created");
   }

   if (walletsRegistered_) {
      //wallet->registerWallet(armoryPtr_);
   }
}

void bs::sync::WalletsManager::balanceUpdated(const std::string &walletId)
{
   addToQueue([this, walletId] {
      QMetaObject::invokeMethod(this, [this, walletId] { emit walletBalanceUpdated(walletId); });
   });
}

void bs::sync::WalletsManager::addressAdded(const std::string &walletId)
{
   addToQueue([this, walletId] {
      QMetaObject::invokeMethod(this, [this, walletId] { emit walletChanged(walletId); });
   });
}

void bs::sync::WalletsManager::metadataChanged(const std::string &walletId)
{
   addToQueue([this, walletId] {
      QMetaObject::invokeMethod(this, [this, walletId] { emit walletMetaChanged(walletId); });
   });
}

void bs::sync::WalletsManager::walletReset(const std::string &walletId)
{
   addToQueue([this, walletId] {
      QMetaObject::invokeMethod(this, [this, walletId] { emit walletChanged(walletId); });
   });
}

void bs::sync::WalletsManager::saveWallet(const HDWalletPtr &wallet)
{
   const auto existingHdWallet = getHDWalletById(wallet->walletId());

   if (existingHdWallet) {    // merge if HD wallet already exists
      existingHdWallet->merge(*wallet);
   }
   else {
      hdWallets_.push_back(wallet);
   }

   for (const auto &leaf : wallet->getLeaves()) {
      addWallet(leaf, true);
   }

   // Update wallet list (fix problem with non-updated wallets list if armory disconnected)
   emit walletChanged(wallet->walletId());
}

void bs::sync::WalletsManager::walletCreated(const std::string &walletId)
{
   const auto &lbdMaint = [this, walletId] {
      for (const auto &hdWallet : hdWallets_) {
         const auto leaf = hdWallet->getLeaf(walletId);
         if (leaf == nullptr) {
            continue;
         }
         logger_->debug("[WalletsManager::walletCreated] HD leaf {} ({}) added"
            , walletId, leaf->name());

         addWallet(leaf);

         QMetaObject::invokeMethod(this, [this, walletId] { emit walletChanged(walletId); });
         break;
      }
   };
   addToQueue(lbdMaint);
}

void bs::sync::WalletsManager::walletDestroyed(const std::string &walletId)
{
   addToQueue([this, walletId] {
      const auto &wallet = getWalletById(walletId);
      eraseWallet(wallet);
      QMetaObject::invokeMethod(this, [this, walletId] { emit walletChanged(walletId); });
   });
}

bs::sync::WalletsManager::HDWalletPtr bs::sync::WalletsManager::getPrimaryWallet() const
{
   for (const auto &wallet : hdWallets_) {
      if (wallet->isPrimary()) {
         return wallet;
      }
   }
   return nullptr;
}

bool bs::sync::WalletsManager::hasPrimaryWallet() const
{
   return (getPrimaryWallet() != nullptr);
}

bs::sync::WalletsManager::WalletPtr bs::sync::WalletsManager::getDefaultWallet() const
{
   WalletPtr result;
   const auto &priWallet = getPrimaryWallet();
   if (priWallet) {
      const auto &group = priWallet->getGroup(priWallet->getXBTGroupType());

      //all leaf paths are always hardened
      const bs::hd::Path leafPath({ bs::hd::Purpose::Native, priWallet->getXBTGroupType(), 0});
      result = group ? group->getLeaf(leafPath) : nullptr;
   }
   return result;
}

bs::sync::WalletsManager::HDWalletPtr bs::sync::WalletsManager::getHDWalletById(const std::string& walletId) const
{
   auto it = std::find_if(hdWallets_.cbegin(), hdWallets_.cend(), [walletId](const HDWalletPtr &hdWallet) {
      return (hdWallet->walletId() == walletId);
   });
   if (it != hdWallets_.end()) {
      return *it;
   }
   return nullptr;
}

bs::sync::WalletsManager::HDWalletPtr bs::sync::WalletsManager::getHDRootForLeaf(const std::string& walletId) const
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

std::vector<bs::sync::WalletsManager::WalletPtr> bs::sync::WalletsManager::getAllWallets() const
{
   QMutexLocker lock(&mtxWallets_);
   std::vector<WalletPtr> result;
   for (const auto &wallet : wallets_) {
      result.push_back(wallet.second);
   }
   return result;
}

bs::sync::WalletsManager::WalletPtr bs::sync::WalletsManager::getWalletById(const std::string& walletId) const
{
   for (const auto &wallet : wallets_) {
      if (wallet.second->hasId(walletId)) {
         return wallet.second;
      }
   }
   return nullptr;
}

bs::sync::WalletsManager::WalletPtr bs::sync::WalletsManager::getWalletByAddress(const bs::Address &address) const
{
   for (const auto &wallet : wallets_) {
      if (wallet.second && (wallet.second->containsAddress(address)
         || wallet.second->containsHiddenAddress(address))) {
         return wallet.second;
      }
   }
   return nullptr;
}

bs::sync::WalletsManager::GroupPtr bs::sync::WalletsManager::getGroupByWalletId(const std::string& walletId) const
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

bool bs::sync::WalletsManager::walletNameExists(const std::string &walletName) const
{
   const auto &it = walletNames_.find(walletName);
   return (it != walletNames_.end());
}

BTCNumericTypes::balance_type bs::sync::WalletsManager::getSpendableBalance() const
{
   if (!isArmoryReady()) {
      return std::numeric_limits<double>::infinity();
   }
   // TODO: make it lazy init
   BTCNumericTypes::balance_type totalSpendable = 0;

   for (const auto& it : wallets_) {
      if (it.second->type() != bs::core::wallet::Type::Bitcoin) {
         continue;
      }
      const auto walletSpendable = it.second->getSpendableBalance();
      if (walletSpendable > 0) {
         totalSpendable += walletSpendable;
      }
   }
   return totalSpendable;
}

BTCNumericTypes::balance_type bs::sync::WalletsManager::getUnconfirmedBalance() const
{
   return getBalanceSum([](const WalletPtr &wallet) {
      return wallet->type() == core::wallet::Type::Bitcoin ? wallet->getUnconfirmedBalance() : 0;
   });
}

BTCNumericTypes::balance_type bs::sync::WalletsManager::getTotalBalance() const
{
   return getBalanceSum([](const WalletPtr &wallet) {
      return wallet->type() == core::wallet::Type::Bitcoin ? wallet->getTotalBalance() : 0;
   });
}

BTCNumericTypes::balance_type bs::sync::WalletsManager::getBalanceSum(
   const std::function<BTCNumericTypes::balance_type(const WalletPtr &)> &cb) const
{
   if (!isArmoryReady()) {
      return 0;
   }
   BTCNumericTypes::balance_type balance = 0;

   for (const auto& it : wallets_) {
      balance += cb(it.second);
   }
   return balance;
}

void bs::sync::WalletsManager::onNewBlock(unsigned int, unsigned int)
{
   QMetaObject::invokeMethod(this, [this] {emit blockchainEvent(); });
}

void bs::sync::WalletsManager::onStateChanged(ArmoryState state)
{
   if (state == ArmoryState::Ready) {
      logger_->debug("[{}] DB ready", __func__);
   }
   else {
      logger_->debug("[WalletsManager::{}] -  Armory state changed: {}"
         , __func__, (int)state);
   }
}

void bs::sync::WalletsManager::walletReady(const std::string &walletId)
{
   QMetaObject::invokeMethod(this, [this, walletId] { emit walletIsReady(walletId); });
   const auto rootWallet = getHDRootForLeaf(walletId);
   if (rootWallet) {
      const auto &itWallet = newWallets_.find(rootWallet->walletId());
      if (itWallet != newWallets_.end()) {
         newWallets_.erase(itWallet);
         rootWallet->synchronize([this, rootWallet] {
            QMetaObject::invokeMethod(this, [this, rootWallet] {
               for (const auto &leaf : rootWallet->getLeaves()) {
                  addWallet(leaf, true);
               }
               emit walletAdded(rootWallet->walletId());
               emit walletsReady();
               logger_->debug("[WalletsManager] wallets are ready after rescan");
            });
         });
      }
      else {
         logger_->debug("[{}] wallet {} completed registration", __func__, walletId);
         emit walletBalanceUpdated(walletId);
      }
   }

   readyWallets_.insert(walletId);
   auto nbWallets = wallets_.size();
   if (readyWallets_.size() >= nbWallets) {
      isReady_ = true;
      logger_->debug("[WalletsManager::{}] All wallets are ready", __func__);
      emit walletsReady();
      readyWallets_.clear();
   }
}

void bs::sync::WalletsManager::scanComplete(const std::string &walletId)
{
   logger_->debug("[{}] - HD wallet {} imported", __func__, walletId);
   const auto hdWallet = getHDWalletById(walletId);
   if (hdWallet) {
      //hdWallet->registerWallet(armoryPtr_);
   }
   QMetaObject::invokeMethod(this, [this, walletId] {
      emit walletChanged(walletId);
      emit walletImportFinished(walletId);
   });
}

bool bs::sync::WalletsManager::isArmoryReady() const
{
   return (armory_ && (armory_->state() == ArmoryState::Ready));
}

void bs::sync::WalletsManager::eraseWallet(const WalletPtr &wallet)
{
   if (!wallet) {
      return;
   }
   QMutexLocker lock(&mtxWallets_);
   wallets_.erase(wallet->walletId());
}

bool bs::sync::WalletsManager::deleteWallet(WalletPtr wallet, bool deleteRemotely)
{
   bool isHDLeaf = false;
   logger_->info("[WalletsManager::{}] - Removing wallet {} ({})...", __func__
      , wallet->name(), wallet->walletId());
   for (auto hdWallet : hdWallets_) {
      const auto leaves = hdWallet->getLeaves();
      if (std::find(leaves.begin(), leaves.end(), wallet) != leaves.end()) {
         for (auto group : hdWallet->getGroups()) {
            if (group->deleteLeaf(wallet)) {
               isHDLeaf = true;
               if (deleteRemotely) {
                  signContainer_->DeleteHDLeaf(wallet->walletId());
               }
               eraseWallet(wallet);
               break;
            }
         }
      }
      if (isHDLeaf) {
         break;
      }
   }

   //wallet->unregisterWallet();
   if (!isHDLeaf) {
      eraseWallet(wallet);
   }

   emit walletDeleted(wallet->walletId());
   emit walletBalanceUpdated(wallet->walletId());
   return true;
}

bool bs::sync::WalletsManager::deleteWallet(HDWalletPtr wallet, bool deleteRemotely)
{
   const auto itHdWallet = std::find(hdWallets_.cbegin(), hdWallets_.cend(), wallet);
   if (itHdWallet == hdWallets_.end()) {
      logger_->warn("[WalletsManager::{}] - Unknown HD wallet {} ({})", __func__
         , wallet->name(), wallet->walletId());
      return false;
   }

   const auto &leaves = wallet->getLeaves();
   const bool prevState = blockSignals(true);
   for (const auto &leaf : leaves) {
      //leaf->unregisterWallet();
   }
   for (const auto &leaf : leaves) {
      eraseWallet(leaf);
   }
   blockSignals(prevState);

   hdWallets_.erase(itHdWallet);
   walletNames_.erase(wallet->name());

   bool result = true;
   if (deleteRemotely) {
      result = wallet->deleteRemotely();
      logger_->info("[WalletsManager::{}] - Wallet {} ({}) removed: {}", __func__
         , wallet->name(), wallet->walletId(), result);
   }

   emit walletDeleted(wallet->walletId());
   emit walletBalanceUpdated(wallet->walletId());
   return result;
}

std::vector<std::string> bs::sync::WalletsManager::registerWallets()
{
   std::vector<std::string> result;
   if (!armory_) {
      logger_->warn("[WalletsManager::{}] armory is not set", __func__);
      return result;
   }
   walletsRegistered_ = true;
   if (empty()) {
      logger_->debug("[WalletsManager::{}] no wallets to register", __func__);
      return result;
   }
   for (auto &it : wallets_) {
      /*const auto& ids = it.second->registerWallet(armoryPtr_);
      result.insert(result.end(), ids.begin(), ids.end());
      if (ids.empty() && it.second->type() != bs::core::wallet::Type::Settlement) {
         logger_->error("[{}] failed to register wallet {}", __func__, it.second->walletId());
      }*/
   }

   return result;
}

void bs::sync::WalletsManager::unregisterWallets()
{
   walletsRegistered_ = false;
   for (auto &it : wallets_) {
      //it.second->unregisterWallet();
   }
}

bool bs::sync::WalletsManager::getTransactionDirection(Tx tx, const std::string &walletId
   , const std::function<void(Transaction::Direction, std::vector<bs::Address>)> &cb)
{
   if (!tx.isInitialized()) {
      logger_->error("[WalletsManager::{}] TX not initialized", __func__);
      return false;
   }

   if (!armory_) {
      logger_->error("[WalletsManager::{}] armory not set", __func__);
      return false;
   }

   const auto wallet = getWalletById(walletId);
   if (!wallet) {
      logger_->error("[WalletsManager::{}] failed to get wallet for id {}"
         , __func__, walletId);
      return false;
   }

   if (wallet->type() == bs::core::wallet::Type::Authentication) {
      cb(Transaction::Auth, {});
      return true;
   }
   else if (wallet->type() == bs::core::wallet::Type::ColorCoin) {
      cb(Transaction::Delivery, {});
      return true;
   }

   const auto group = getGroupByWalletId(walletId);
   if (!group) {
      logger_->warn("[{}] group for {} not found", __func__, walletId);
   }

   const std::string txKey = tx.getThisHash().toBinStr() + walletId;
   auto dir = Transaction::Direction::Unknown;
   std::vector<bs::Address> inAddrs;
   {
      FastLock lock(txDirLock_);
      const auto &itDirCache = txDirections_.find(txKey);
      if (itDirCache != txDirections_.end()) {
         dir = itDirCache->second.first;
         inAddrs = itDirCache->second.second;
      }
   }
   if (dir != Transaction::Direction::Unknown) {
      cb(dir, inAddrs);
      return true;
   }

   std::set<BinaryData> opTxHashes;
   std::map<BinaryData, std::vector<uint32_t>> txOutIndices;

   for (size_t i = 0; i < tx.getNumTxIn(); ++i) {
      TxIn in = tx.getTxInCopy((int)i);
      OutPoint op = in.getOutPoint();

      opTxHashes.insert(op.getTxHash());
      txOutIndices[op.getTxHash()].push_back(op.getTxOutIndex());
   }

   const auto &cbProcess = [this, wallet, group, tx, txKey, txOutIndices, cb]
      (const AsyncClient::TxBatchResult &txs, std::exception_ptr)
   {
      bool ourOuts = false;
      bool otherOuts = false;
      bool ourIns = false;
      bool otherIns = false;
      bool ccTx = false;

      std::vector<TxOut> txOuts;
      std::vector<bs::Address> inAddrs;
      txOuts.reserve(tx.getNumTxIn());
      inAddrs.reserve(tx.getNumTxIn());

      for (const auto &prevTx : txs) {
         if (!prevTx.second) {
            continue;
         }
         const auto &itIdx = txOutIndices.find(prevTx.first);
         if (itIdx == txOutIndices.end()) {
            continue;
         }
         for (const auto idx : itIdx->second) {
            TxOut prevOut = prevTx.second->getTxOutCopy((int)idx);
            const auto addr = bs::Address::fromTxOut(prevOut);
            const auto addrWallet = getWalletByAddress(addr);
            const auto addrGroup = addrWallet ? getGroupByWalletId(addrWallet->walletId()) : nullptr;
            (((addrWallet == wallet) || (group && (group == addrGroup))) ? ourIns : otherIns) = true;
            if (addrWallet && (addrWallet->type() == bs::core::wallet::Type::ColorCoin)) {
               ccTx = true;
            }
            txOuts.emplace_back(prevOut);
            inAddrs.emplace_back(std::move(addr));
         }
      }

      for (size_t i = 0; i < tx.getNumTxOut(); ++i) {
         try {
            TxOut out = tx.getTxOutCopy((int)i);
            const auto addrObj = bs::Address::fromTxOut(out);
            const auto addrWallet = getWalletByAddress(addrObj);
            const auto addrGroup = addrWallet ? getGroupByWalletId(addrWallet->walletId()) : nullptr;
            (((addrWallet == wallet) || (group && (group == addrGroup))) ? ourOuts : otherOuts) = true;
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
         catch (...) {
            otherOuts = true;
         }
      }

      if (wallet->type() == bs::core::wallet::Type::Settlement) {
         if (ourOuts) {
            updateTxDirCache(txKey, Transaction::PayIn, inAddrs, cb);
            return;
         }
         if (txOuts.size() == 1) {
#if 0    //TODO: decide later how to handle settlement addresses
            const bs::Address addr = txOuts[0].getScrAddressStr();
            const auto settlAE = getSettlementWallet()->getAddressEntryForAddr(addr);
            if (settlAE) {
               const auto &cbPayout = [this, cb, txKey, inAddrs](bs::PayoutSigner::Type poType) {
                  if (poType == bs::PayoutSigner::SignedBySeller) {
                     updateTxDirCache(txKey, Transaction::Revoke, inAddrs, cb);
                  }
                  else {
                     updateTxDirCache(txKey, Transaction::PayOut, inAddrs, cb);
                  }
               };
               bs::PayoutSigner::WhichSignature(tx, 0, settlAE, logger_, armoryPtr_, cbPayout);
               return;
            }
            logger_->warn("[WalletsManager::{}] - failed to get settlement AE"
               , __func__);
#endif   //0
         }
         else {
            logger_->warn("[WalletsManager::{}] - more than one settlement "
               "output", __func__);
         }
         updateTxDirCache(txKey, Transaction::PayOut, inAddrs, cb);
         return;
      }

      if (ccTx) {
         updateTxDirCache(txKey, Transaction::Payment, inAddrs, cb);
         return;
      }
      if (ourOuts && ourIns && !otherOuts && !otherIns) {
         updateTxDirCache(txKey, Transaction::Internal, inAddrs, cb);
         return;
      }
      if (!ourIns) {
         updateTxDirCache(txKey, Transaction::Received, inAddrs, cb);
         return;
      }
      if (otherOuts) {
         updateTxDirCache(txKey, Transaction::Sent, inAddrs, cb);
         return;
      }
      updateTxDirCache(txKey, Transaction::Unknown, inAddrs, cb);
   };
   if (opTxHashes.empty()) {
      logger_->error("[WalletsManager::{}] - empty TX hashes", __func__);
      return false;
   }
   else {
      armory_->getTXsByHash(opTxHashes, cbProcess, true);
   }
   return true;
}

bool bs::sync::WalletsManager::getTransactionMainAddress(const Tx &tx, const std::string &walletId
   , bool isReceiving, const std::function<void(QString, int)> &cb)
{
   if (!tx.isInitialized() || !armory_) {
      return false;
   }
   const auto wallet = getWalletById(walletId);
   if (!wallet) {
      return false;
   }

   const std::string txKey = tx.getThisHash().toBinStr() + walletId;
   const auto &itDesc = txDesc_.find(txKey);
   if (itDesc != txDesc_.end()) {
      cb(itDesc->second.first, itDesc->second.second);
      return true;
   }

   const bool isSettlement = (wallet->type() == bs::core::wallet::Type::Settlement);
   std::set<bs::Address> ownAddresses, foreignAddresses;
   for (size_t i = 0; i < tx.getNumTxOut(); ++i) {
      TxOut out = tx.getTxOutCopy((int)i);
      try {
         const auto addr = bs::Address::fromTxOut(out);
         const auto addrWallet = getWalletByAddress(addr);
         if (addrWallet == wallet) {
            ownAddresses.insert(addr);
         } else {
            foreignAddresses.insert(addr);
         }
      }
      catch (const std::exception &) {
         // address conversion failure - likely OP_RETURN - do nothing
      }
   }

   if (!isReceiving && (ownAddresses.size() == 1) && !foreignAddresses.empty()) {
      if (!wallet->isExternalAddress(*ownAddresses.begin())) {
         ownAddresses.clear();   // treat the only own internal address as change and throw away
      }
   }

   const auto &lbdProcessAddresses = [this, txKey, cb](const std::set<bs::Address> &addresses) {
      switch (addresses.size()) {
      case 0:
         updateTxDescCache(txKey, tr("no address"), (int)addresses.size(), cb);
         break;

      case 1:
         updateTxDescCache(txKey, QString::fromStdString((*addresses.begin()).display())
            , (int)addresses.size(), cb);
         break;

      default:
         updateTxDescCache(txKey, tr("%1 output addresses").arg(addresses.size()), (int)addresses.size(), cb);
         break;
      }
   };

   if (!ownAddresses.empty()) {
      lbdProcessAddresses(ownAddresses);
   }
   else {
      lbdProcessAddresses(foreignAddresses);
   }
   return true;
}

void bs::sync::WalletsManager::updateTxDirCache(const std::string &txKey, Transaction::Direction dir
   , const std::vector<bs::Address> &inAddrs
   , std::function<void(Transaction::Direction, std::vector<bs::Address>)> cb)
{
   {
      FastLock lock(txDirLock_);
      txDirections_[txKey] = { dir, inAddrs };
   }
   cb(dir, inAddrs);
}

void bs::sync::WalletsManager::updateTxDescCache(const std::string &txKey, const QString &desc, int addrCount, std::function<void(QString, int)> cb)
{
   {
      FastLock lock(txDescLock_);
      txDesc_[txKey] = { desc, addrCount };
   }
   cb(desc, addrCount);
}

#if 0
void bs::sync::WalletsManager::createSettlementLeaf(const bs::Address &authAddr
   , const std::function<void(const SecureBinaryData &)> &cb)
{
   if (!signContainer_) {
      logger_->error("[{}] signer is not set - aborting", __func__);
      if (cb) {
         cb({});
      }
      return;
   }
   const auto cbWrap = [this, cb](const SecureBinaryData &pubKey) {
      const auto priWallet = getPrimaryWallet();
      if (!priWallet) {
         logger_->error("[WalletsManager::createSettlementLeaf] no primary wallet");
         return;
      }
      priWallet->synchronize([this, priWallet] {
         const auto group = priWallet->getGroup(bs::hd::BlockSettle_Settlement);
         if (!group) {
            logger_->error("[WalletsManager::createSettlementLeaf] no settlement group");
            return;
         }
         unsigned int nbSettlLeaves = 0;
         for (const auto &settlLeaf : group->getLeaves()) {
            if (getWalletById(settlLeaf->walletId()) != nullptr) {
               logger_->warn("[WalletsManager::createSettlementLeaf] leaf {} already exists", settlLeaf->walletId());
               continue;
            }
            addWallet(settlLeaf, true);
            emit walletAdded(settlLeaf->walletId());
            nbSettlLeaves++;
         }
         if (nbSettlLeaves) {
            emit settlementLeavesLoaded(nbSettlLeaves);
         }
      });
      if (cb) {
         cb(pubKey);
      }
   };
   signContainer_->createSettlementWallet(authAddr, cbWrap);
}
#endif   //0

void bs::sync::WalletsManager::startWalletRescan(const HDWalletPtr &hdWallet)
{
   if (armory_->state() == ArmoryState::Ready) {
      hdWallet->startRescan();
   }
   else {
      logger_->error("[{}] invalid Armory state {}", __func__, (int)armory_->state());
   }
}

void bs::sync::WalletsManager::onWalletsListUpdated()
{
   const auto &cbSyncWallets = [this](const std::vector<bs::sync::WalletInfo> &wi) {
      std::map<std::string, bs::sync::WalletInfo> hdWallets;
      for (const auto &info : wi) {
         hdWallets[*info.ids.cbegin()] = info;
      }
      for (const auto &hdWallet : hdWallets) {
         const auto &itHdWallet = std::find_if(hdWallets_.cbegin(), hdWallets_.cend()
            , [walletId=hdWallet.first](const HDWalletPtr &wallet) {
               return (wallet->walletId() == walletId);
         });
         if (itHdWallet == hdWallets_.end()) {
            syncWallet(hdWallet.second, [this, hdWalletId=hdWallet.first] {
               const auto hdWallet = getHDWalletById(hdWalletId);
            });
            newWallets_.insert(hdWallet.first);
         }
         else {
            const auto wallet = *itHdWallet;
            const auto &cbSyncHD = [this, wallet](bs::sync::HDWalletData hdData) {
               bool walletUpdated = false;
               if (hdData.groups.size() != wallet->getGroups().size()) {
                  walletUpdated = true;
               }
               else {
                  for (const auto &group : hdData.groups) {
                     const auto hdGroup = wallet->getGroup(group.type);
                     if (hdGroup->getLeaves().size() != group.leaves.size()) {
                        walletUpdated = true;
                        break;
                     }
                  }
               }
               if (!walletUpdated) {
                  return;
               }
               logger_->debug("[WalletsManager::onWalletsListUpdated] wallet {} has changed - resyncing"
                  , wallet->walletId());
               wallet->synchronize([this, wallet] {
                  for (const auto &leaf : wallet->getLeaves()) {
                     if (!getWalletById(leaf->walletId())) {
                        logger_->debug("[WalletsManager::onWalletsListUpdated] adding new leaf {}"
                           , leaf->walletId());
                        addWallet(leaf, true);
                     }
                  }
                  wallet->scan([this, wallet](bs::sync::SyncState state) {
                     if (state == bs::sync::SyncState::Success) {
                        QMetaObject::invokeMethod(this, [this, wallet] {
                           emit walletChanged(wallet->walletId());
                        });
                     }
                  });
               });
            };
            signContainer_->syncHDWallet(wallet->walletId(), cbSyncHD);
         }
      }
      std::vector<std::string> hdWalletsId;
      hdWalletsId.reserve(hdWallets_.size());
      for (const auto &hdWallet : hdWallets_) {
         hdWalletsId.push_back(hdWallet->walletId());
      }
      for (const auto &hdWalletId : hdWalletsId) {
         if (hdWallets.find(hdWalletId) == hdWallets.end()) {
            deleteWallet(getHDWalletById(hdWalletId), false);
         }
      }
   };
   signContainer_->syncWalletInfo(cbSyncWallets);
}

#if 0
void bs::sync::WalletsManager::onAuthLeafAdded(const std::string &walletId)
{
   if (walletId.empty()) {
      if (authAddressWallet_) {
         logger_->debug("[WalletsManager::onAuthLeafAdded] auth wallet {} unset", authAddressWallet_->walletId());
         deleteWallet(authAddressWallet_, false);
      }
      return;
   }
   const auto wallet = getPrimaryWallet();
   if (!wallet) {
      logger_->error("[WalletsManager::onAuthLeafAdded] no primary wallet loaded");
      return;
   }
   auto group = wallet->getGroup(bs::hd::CoinType::BlockSettle_Auth);
   if (!group) {
      logger_->error("[WalletsManager::onAuthLeafAdded] no auth group in primary wallet");
      return;
   }

   const bs::hd::Path authPath({ bs::hd::Purpose::Native, bs::hd::CoinType::BlockSettle_Auth, 0 });
   logger_->debug("[WalletsManager::onAuthLeafAdded] creating auth leaf with id {}", walletId);
   auto leaf = group->getLeaf(authPath);
   if (leaf) {
      logger_->warn("[WalletsManager::onAuthLeafAdded] auth leaf already exists");
      group->deleteLeaf(authPath);
   }
   try {
      const bs::hd::Path authPath({ static_cast<bs::hd::Path::Elem>(bs::hd::Purpose::Native)
         , bs::hd::CoinType::BlockSettle_Auth, 0 });
      leaf = group->createLeaf(authPath, walletId);
   }
   catch (const std::exception &e) {
      logger_->error("[WalletsManager::onAuthLeafAdded] failed to create auth leaf: {}", e.what());
      return;
   }
   leaf->synchronize([this, leaf] {
      logger_->debug("[WalletsManager::onAuthLeafAdded sync cb] Synchronized auth leaf has {} address[es]", leaf->getUsedAddressCount());
      addWallet(leaf, true);
      authAddressWallet_ = leaf;
      QMetaObject::invokeMethod(this, [this, walletId=leaf->walletId()] {
         emit walletChanged(walletId);
      });
   });
}
#endif   //0

void bs::sync::WalletsManager::adoptNewWallet(const HDWalletPtr &wallet)
{
   saveWallet(wallet);
   emit newWalletAdded(wallet->walletId());
   emit walletsReady();
}

void bs::sync::WalletsManager::addWallet(const HDWalletPtr &wallet)
{
   if (!wallet) {
      return;
   }
   saveWallet(wallet);
   if (armory_) {
      //wallet->registerWallet(armoryPtr_);
      emit walletsReady();
   }
}

bool bs::sync::WalletsManager::isWatchingOnly(const std::string &walletId) const
{
   if (signContainer_) {
      return signContainer_->isWalletOffline(walletId);
   }
   return false;
}

void bs::sync::WalletsManager::goOnline()
{
/*   trackersStarted_ = true;
   for (const auto &cc : ccResolver_->securities()) {
      startTracker(cc);
   }*/
}

#if 0
void bs::sync::WalletsManager::onCCSecurityInfo(QString ccProd, unsigned long nbSatoshis, QString genesisAddr)
{
   const auto &cc = ccProd.toStdString();
   logger_->debug("[{}] received info for {}", __func__, cc);
   const auto genAddr = bs::Address::fromAddressString(genesisAddr.toStdString());
   ccResolver_->addData(cc, nbSatoshis, genAddr);

   if (trackersStarted_) {
      startTracker(ccProd.toStdString());
   }
}

void bs::sync::WalletsManager::onCCInfoLoaded()
{
   logger_->debug("[WalletsManager::{}] - Re-validating against GAs in CC leaves"
      , __func__);
   for (const auto &wallet : wallets_) {
      if (wallet.second->type() != bs::core::wallet::Type::ColorCoin) {
         continue;
      }
      const auto ccWallet = std::dynamic_pointer_cast<bs::sync::hd::CCLeaf>(wallet.second);
      if (ccWallet) {
         ccWallet->setCCDataResolver(ccResolver_);
         // Update CC tracker one more time because ccWallet->suffix_ could be updated here
         updateTracker(ccWallet);
      }
      else {
         logger_->warn("[{}] invalid CC leaf {}", __func__, wallet.second->walletId());
      }
   }
   for (const auto &hdWallet : hdWallets_) {
      for (const auto &leaf : hdWallet->getLeaves()) {
         if (leaf->type() == bs::core::wallet::Type::ColorCoin) {
            leaf->init();
         }
      }
   }
}
#endif   //0

// The initial point for processing an incoming zero conf TX. Important notes:
//
// - When getting the ZC list from Armory, previous ZCs won't clear out until
//   they have been confirmed.
// - If a TX has multiple instances of the same address, each instance will get
//   its own UTXO object while sharing the same UTXO hash.
// - It is possible, in conjunction with a wallet, to determine if the UTXO is
//   attached to an internal or external address.
void bs::sync::WalletsManager::onZCReceived(const std::string& , const std::vector<bs::TXEntry>& entries)
{
   std::vector<bs::TXEntry> ourZCentries;

   for (const auto &entry : entries) {
      for (const auto &walletId : entry.walletIds) {
         const auto wallet = getWalletById(walletId);
         if (!wallet) {
            continue;
         }
         logger_->debug("[WalletsManager::onZCReceived] - ZC entry in wallet {}"
            , wallet->name());

         // We have an affected wallet. Update it!
         ourZCentries.push_back(entry);
         //wallet->updateBalances();
         break;
      }
   } // for

     // Emit signals for the wallet and TX view models.
   QMetaObject::invokeMethod(this, [this] {emit blockchainEvent(); });
   if (!ourZCentries.empty()) {
      QMetaObject::invokeMethod(this, [this, ourZCentries] { emit newTransactions(ourZCentries); });
   }
}

void bs::sync::WalletsManager::onZCInvalidated(const std::set<BinaryData> &ids)
{
   QMetaObject::invokeMethod(this, [this, ids] {emit invalidatedZCs(ids); });
}

void bs::sync::WalletsManager::onTxBroadcastError(const std::string& , const BinaryData &txHash, int errCode, const std::string &errMsg)
{
   logger_->error("[WalletsManager::onTxBroadcastError] - TX {} error: {} ({})"
      , txHash.toHexStr(true), errCode, errMsg);
}

void bs::sync::WalletsManager::invokeFeeCallbacks(unsigned int blocks, float fee)
{
   std::vector<QObject *> objsToDelete;
   for (auto &cbByObj : feeCallbacks_) {
      const auto &it = cbByObj.second.find(blocks);
      if (it == cbByObj.second.end()) {
         continue;
      }
      if (!it->second.first) {
         break;
      }
      it->second.second(fee);
      cbByObj.second.erase(it);
      if (cbByObj.second.empty()) {
         objsToDelete.push_back(cbByObj.first);
      }
   }
   for (const auto &obj : objsToDelete) {
      feeCallbacks_.erase(obj);
   }
}

bool bs::sync::WalletsManager::estimatedFeePerByte(unsigned int blocksToWait, std::function<void(float)> cb, QObject *obj)
{
   if (!armory_) {
      return false;
   }
   auto blocks = blocksToWait;
   if (blocks < 2) {
      blocks = 2;
   } else if (blocks > 1008) {
      blocks = 1008;
   }

   if (lastFeePerByte_[blocks].isValid() && (lastFeePerByte_[blocks].secsTo(QDateTime::currentDateTime()) < 30)) {
      cb(feePerByte_[blocks]);
      return true;
   }

   bool callbackRegistered = false;
   for (const auto &cbByObj : feeCallbacks_) {
      if (cbByObj.second.find(blocks) != cbByObj.second.end()) {
         callbackRegistered = true;
         break;
      }
   }
   feeCallbacks_[obj][blocks] = { obj, cb };
   if (callbackRegistered) {
      return true;
   }
   const auto &cbFee = [this, blocks](float fee) {
      if (fee == std::numeric_limits<float>::infinity()) {
         invokeFeeCallbacks(blocks, fee);
         return;
      }
      fee = ArmoryConnection::toFeePerByte(fee);
      if (fee != 0) {
         feePerByte_[blocks] = fee;
         lastFeePerByte_[blocks] = QDateTime::currentDateTime();
         invokeFeeCallbacks(blocks, fee);
         return;
      }

      SPDLOG_LOGGER_WARN(logger_, "Fees estimation are not available, use hardcoded values!");
      if (blocks > 3) {
         feePerByte_[blocks] = 50;
      }
      else if (blocks >= 2) {
         feePerByte_[blocks] = 100;
      }
      invokeFeeCallbacks(blocks, feePerByte_[blocks]);
   };
   return armory_->estimateFee(blocks, cbFee);
}

bool bs::sync::WalletsManager::getFeeSchedule(const std::function<void(const std::map<unsigned int, float> &)> &cb)
{
   if (!armory_) {
      return false;
   }
   return armory_->getFeeSchedule(cb);
}

void bs::sync::WalletsManager::trackAddressChainUse(
   std::function<void(bool)> cb)
{
   /***
   This method grabs address txn count from the db for all managed
   wallets and deduces address chain use and type from the address
   tx counters.

   This is then reflected to the armory wallets through the
   SignContainer, to keep address chain counters and address types
   in sync.

   This method should be run only once per per, after registration.

   It will only have an effect if a wallet has been restored from
   seed or if there exist several instances of a wallet being used
   on different machines across time.

   More often than not, the armory wallet has all this meta data
   saved on disk to begin with.

   Callback is fired with either true (operation success) or
   false (SyncState_Failure, read below):

   trackChainAddressUse can return 3 states per wallet. These
   states are combined and processed as one when all wallets are
   done synchronizing. The states are as follow:

    - SyncState_Failure: the armory wallet failed to fine one or
      several of the addresses. This shouldn't typically happen.
      Most likely culprit is an address chain that is too short.
      Extend it.
      This state overrides all other states.

    - SyncState_NothingToDo: wallets are already sync'ed.
      Lowest priority.

    - SyncState_Success: Armory wallet address chain usage is now up
      to date, call WalletsManager::SyncWallets once again.
      Overrides NothingToDo.
   ***/

   auto ctr = std::make_shared<std::atomic<unsigned>>(0);
   auto wltCount = wallets_.size();
   auto state = std::make_shared<bs::sync::SyncState>(bs::sync::SyncState::NothingToDo);

   for (auto &it : wallets_)
   {
      auto trackLbd = [this, ctr, wltCount, state, cb](bs::sync::SyncState st)->void
      {
         switch (st)
         {
         case bs::sync::SyncState::Failure:
            *state = st;
            break;

         case bs::sync::SyncState::Success:
         {
            if (*state == bs::sync::SyncState::NothingToDo)
               *state = st;
            break;
         }

         default:
            break;
         }

         if (ctr->fetch_add(1) == wltCount - 1)
         {
            switch (*state)
            {
            case bs::sync::SyncState::Failure:
            {
               cb(false);
               return;
            }

            case bs::sync::SyncState::Success:
            {
               auto progLbd = [cb](int curr, int tot)->void
               {
                  if (curr == tot)
                     cb(true);
               };

               syncWallets(progLbd);
               return;
            }

            default:
               cb(true);
               return;
            }
         }
      };

      auto leafPtr = it.second;
      auto countLbd = [leafPtr, trackLbd](void)->void
      {
         leafPtr->trackChainAddressUse(trackLbd);
      };

      if (!leafPtr->getAddressTxnCounts(countLbd)) {
         cb(false);
      }
   }
}

void bs::sync::WalletsManager::addToQueue(const MaintQueueCb &cb)
{
   std::unique_lock<std::mutex> lock(mutex_);
   queue_.push_back(cb);
   queueCv_.notify_one();
}

void bs::sync::WalletsManager::threadFunction()
{
   while (threadRunning_) {
      {
         std::unique_lock<std::mutex> lock(mutex_);
         if (queue_.empty()) {
            queueCv_.wait_for(lock, std::chrono::milliseconds{ 500 });
         }
      }
      if (!threadRunning_) {
         break;
      }
      decltype(queue_) tempQueue;
      {
         std::unique_lock<std::mutex> lock(mutex_);
         tempQueue.swap(queue_);
      }
      if (tempQueue.empty()) {
         continue;
      }

      for (const auto &cb : tempQueue) {
         if (!threadRunning_) {
            break;
         }
         cb();
      }
   }
}

#if 0
void bs::sync::WalletsManager::CCResolver::addData(const std::string &cc, uint64_t lotSize
   , const bs::Address &genAddr)
{
   securities_[cc] = { lotSize, genAddr };
   const auto walletIdx = bs::hd::Path::keyToElem(cc) | bs::hd::hardFlag;
   walletIdxMap_[walletIdx] = cc;
}

std::vector<std::string> bs::sync::WalletsManager::CCResolver::securities() const
{
   std::vector<std::string> result;
   for (const auto &ccDef : securities_) {
      result.push_back(ccDef.first);
   }
   return result;
}

std::string bs::sync::WalletsManager::CCResolver::nameByWalletIndex(bs::hd::Path::Elem idx) const
{
   idx |= bs::hd::hardFlag;
   const auto &itWallet = walletIdxMap_.find(idx);
   if (itWallet != walletIdxMap_.end()) {
      return itWallet->second;
   }
   return {};
}

uint64_t bs::sync::WalletsManager::CCResolver::lotSizeFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.lotSize;
   }
   return 0;
}

bs::Address bs::sync::WalletsManager::CCResolver::genesisAddrFor(const std::string &cc) const
{
   const auto &itSec = securities_.find(cc);
   if (itSec != securities_.end()) {
      return itSec->second.genesisAddr;
   }
   return {};
}

bool bs::sync::WalletsManager::CreateCCLeaf(const std::string &ccName, const std::function<void(bs::error::ErrorCode result)> &cb)
{
   if (!isCCNameCorrect(ccName)) {
      logger_->error("[WalletsManager::CreateCCLeaf] invalid cc name passed: {}"
                     , ccName);
      return false;
   }

   // try to get cc leaf first, it might exist alread
   if (getCCWallet(ccName) != nullptr) {
      logger_->error("[WalletsManager::CreateCCLeaf] CC leaf already exists: {}"
                     , ccName);
      return false;
   }

   const auto primaryWallet = getPrimaryWallet();
   if (primaryWallet == nullptr) {
      logger_->error("[WalletsManager::CreateCCLeaf] there are no primary wallet. Could not create {}"
                     , ccName);
      return false;
   }

   bs::hd::Path path;

   path.append(static_cast<bs::hd::Path::Elem>(bs::hd::Purpose::Native) | bs::hd::hardFlag);
   path.append(bs::hd::BlockSettle_CC | bs::hd::hardFlag);
   path.append(ccName);

   bs::sync::PasswordDialogData dialogData;
   dialogData.setValue(PasswordDialogData::DialogType
      , ui::getPasswordInputDialogName(ui::PasswordInputDialogType::RequestPasswordForToken));
   dialogData.setValue(PasswordDialogData::Title, tr("Create CC Leaf"));
   dialogData.setValue(PasswordDialogData::Product, QString::fromStdString(ccName));

   const auto &createCCLeafCb = [this, ccName, cb](bs::error::ErrorCode result
      , const std::string &walletId) {
      processCreatedCCLeaf(ccName, result, walletId);
      if (cb) {
         cb(result);
      }
   };

   return signContainer_->createHDLeaf(primaryWallet->walletId(), path, {}, dialogData, createCCLeafCb);
}

void bs::sync::WalletsManager::processCreatedCCLeaf(const std::string &ccName, bs::error::ErrorCode result
   , const std::string &walletId)
{
   if (result == bs::error::ErrorCode::NoError) {
      logger_->debug("[WalletsManager::ProcessCreatedCCLeaf] CC leaf {} created with id {}"
                     , ccName, walletId);

      auto wallet = getPrimaryWallet();
      if (!wallet) {
         logger_->error("[WalletsManager::ProcessCreatedCCLeaf] primary wallet should exist");
         return;
      }

      auto group = wallet->getGroup(bs::hd::CoinType::BlockSettle_CC);
      if (!group) {
         logger_->error("[WalletsManager::ProcessCreatedCCLeaf] missing CC group");
         return;
      }

      const bs::hd::Path ccLeafPath({ bs::hd::Purpose::Native, bs::hd::CoinType::BlockSettle_CC
         , bs::hd::Path::keyToElem(ccName) });
      auto leaf = group->createLeaf(ccLeafPath, walletId);

      addWallet(leaf);
      newWallets_.insert(wallet->walletId());

      leaf->synchronize([this, leaf, ccName] {
         logger_->debug("CC leaf {} synchronized", ccName);
         emit CCLeafCreated(ccName);
      });
   } else {
      logger_->error("[WalletsManager::ProcessCreatedCCLeaf] CC leaf {} creation failed: {}"
                     , ccName, static_cast<int>(result));
      emit CCLeafCreateFailed(ccName, result);
   }
}

bool bs::sync::WalletsManager::PromoteWalletToPrimary(const std::string& walletId)
{
   bs::sync::PasswordDialogData dialogData;
   dialogData.setValue(PasswordDialogData::Title, tr("Promote to primary"));

   const auto& promoteToPriimaryCB = [this](bs::error::ErrorCode result
      , const std::string &walletId) {
      const auto wallet = getHDWalletById(walletId);
      if (!wallet) {
         logger_->error("[WalletsManager::PromoteWalletToPrimary CB] failed to find wallet {}", walletId);
         return;
      }
      wallet->synchronize([this, result, walletId] {
         processPromoteWallet(result, walletId);
      });
   };
   return signContainer_->promoteWalletToPrimary(walletId, dialogData, promoteToPriimaryCB);
}

void bs::sync::WalletsManager::processPromoteWallet(bs::error::ErrorCode result, const std::string& walletId)
{
   if (result == bs::error::ErrorCode::NoError) {
      emit walletPromotedToPrimary(walletId);
      emit walletChanged(walletId);
   } else {
      logger_->error("[WalletsManager::processPromoteWallet] Wallet {} promotion failed: {}"
                     , walletId, static_cast<int>(result));
   }
}

bool bs::sync::WalletsManager::EnableXBTTradingInWallet(const std::string& walletId
   , const std::function<void(bs::error::ErrorCode result)> &cb)
{
   bs::sync::PasswordDialogData dialogData;
   dialogData.setValue(PasswordDialogData::Title, tr("Enable Trading"));
   dialogData.setValue(PasswordDialogData::XBT, tr("Authentification Addresses"));

   const auto& enableTradingCB = [this, cb](bs::error::ErrorCode result
      , const std::string &walletId) {
      const auto wallet = getHDWalletById(walletId);
      if (!wallet) {
         logger_->error("[WalletsManager::EnableXBTTradingInWallet] failed to find wallet {}", walletId);
         if (cb) {
            cb(bs::error::ErrorCode::WalletNotFound);
         }
         return;
      }
      wallet->synchronize([this, cb, result, walletId] {
         processEnableTrading(result, walletId);
         if (cb) {
            cb(result);
         }
      });
   };
   return signContainer_->enableTradingInHDWallet(walletId, userId_, dialogData, enableTradingCB);
}

void bs::sync::WalletsManager::processEnableTrading(bs::error::ErrorCode result, const std::string& walletId)
{
   if (result == bs::error::ErrorCode::NoError) {
      emit walletChanged(walletId);
   } else {
      logger_->error("[WalletsManager::processEnableTrading] Wallet {} promotion failed: {}"
                     , walletId, static_cast<int>(result));
   }
}

void bs::sync::WalletsManager::startTracker(const std::string &cc)
{
   std::unique_ptr<ColoredCoinTrackerInterface> trackerSnapshots;
   if (trackerClient_) {
      trackerSnapshots = CcTrackerClient::createClient(trackerClient_, ccResolver_->lotSizeFor(cc));
   } else {
      trackerSnapshots = std::make_unique<ColoredCoinTracker>(ccResolver_->lotSizeFor(cc), armoryPtr_);
   }
   trackerSnapshots->addOriginAddress(ccResolver_->genesisAddrFor(cc));

   trackerSnapshots->setSnapshotUpdatedCb([this, cc] {
      checkTrackerUpdate(cc);
   });
   trackerSnapshots->setZcSnapshotUpdatedCb([this, cc] {
      checkTrackerUpdate(cc);
   });
   trackerSnapshots->setReadyCb([this, cc] {
      emit ccTrackerReady(cc);
   });

   const auto tracker = std::make_shared<ColoredCoinTrackerClient>(std::move(trackerSnapshots));
   trackers_[cc] = tracker;
   logger_->debug("[{}] added CC tracker for {}", __func__, cc);

   for (const auto &wallet : getAllWallets()) {
      auto ccLeaf = std::dynamic_pointer_cast<bs::sync::hd::CCLeaf>(wallet);
      if (ccLeaf) {
         updateTracker(ccLeaf);
      }
   }

   std::thread([cc, tracker, logger = logger_] {
      if (!tracker->goOnline()) {
         SPDLOG_LOGGER_ERROR(logger, "failed for {}", cc);
      }
   }).detach();
}

void bs::sync::WalletsManager::updateTracker(const std::shared_ptr<hd::CCLeaf> &ccLeaf)
{
   const auto itTracker = trackers_.find(ccLeaf->displaySymbol().toStdString());
   if (itTracker != trackers_.end()) {
      ccLeaf->setCCTracker(itTracker->second);
   }
}

void bs::sync::WalletsManager::checkTrackerUpdate(const std::string &cc)
{
   for (const auto &wallet : getAllWallets()) {
      auto ccLeaf = std::dynamic_pointer_cast<bs::sync::hd::CCLeaf>(wallet);
      if (ccLeaf && ccLeaf->displaySymbol().toStdString() == cc) {
         auto newOutpointMap = ccLeaf->getOutpointMapFromTracker(false);
         auto newOutpointMapZc = ccLeaf->getOutpointMapFromTracker(true);
         std::lock_guard<std::mutex> lock(ccOutpointMapsFromTrackerMutex_);
         auto &outpointMap = ccOutpointMapsFromTracker_[ccLeaf->walletId()];
         auto &outpointMapZc = ccOutpointMapsFromTrackerZc_[ccLeaf->walletId()];
         if (outpointMap != newOutpointMap || outpointMapZc != newOutpointMapZc) {
            outpointMap = std::move(newOutpointMap);
            outpointMapZc = std::move(newOutpointMapZc);
            emit walletBalanceUpdated(wallet->walletId());
         }
         break;
      }
   }
}

bool bs::sync::WalletsManager::createAuthLeaf(const std::function<void()> &cb)
{
   if (getAuthWallet() != nullptr) {
      logger_->error("[WalletsManager::CreateAuthLeaf] auth leaf already exists");
      return false;
   }

   if (userId_.empty()) {
      logger_->error("[WalletsManager::CreateAuthLeaf] can't create auth leaf without user id");
      return false;
   }

   auto primaryWallet = getPrimaryWallet();
   if (primaryWallet == nullptr) {
      logger_->error("[WalletsManager::CreateAuthLeaf] could not create auth leaf - no primary wallet");
      return false;
   }

   const bs::hd::Path authPath({ bs::hd::Purpose::Native, bs::hd::CoinType::BlockSettle_Auth, 0 });
   bs::wallet::PasswordData pwdData;
   pwdData.salt = userId_;
   bs::sync::PasswordDialogData dialogData;
   dialogData.setValue(PasswordDialogData::DialogType
      , ui::getPasswordInputDialogName(ui::PasswordInputDialogType::RequestPasswordForAuthLeaf));
   dialogData.setValue(PasswordDialogData::Title, tr("Create Authentication Address Leaf"));
   dialogData.setValue(PasswordDialogData::Product, QString::fromStdString(userId_.toHexStr()));

   const auto &createAuthLeafCb = [this, cb, primaryWallet, authPath]
      (bs::error::ErrorCode result, const std::string &walletId)
   {
      if (result != bs::error::ErrorCode::NoError) {
         logger_->error("[WalletsManager::createAuthLeaf] auth leaf creation failure: {}"
            , (int)result);
         return;
      }
      const auto group = primaryWallet->getGroup(bs::hd::CoinType::BlockSettle_Auth);
      const auto authGroup = std::dynamic_pointer_cast<bs::sync::hd::AuthGroup>(group);
      if (!authGroup) {
         logger_->error("[WalletsManager::createAuthLeaf] no auth group exists");
         return;
      }
      authGroup->setUserId(userId_);
      const auto leaf = authGroup->createLeaf(authPath, walletId);
      if (!leaf) {
         logger_->error("[WalletsManager::createAuthLeaf] failed to create auth leaf");
         return;
      }
      leaf->synchronize([this, cb, leaf] {
         authAddressWallet_ = leaf;
         addWallet(leaf, true);
         emit AuthLeafCreated();
         emit authWalletChanged();
         emit walletChanged(leaf->walletId());
         if (cb) {
            cb();
         }
      });
   };
   return signContainer_->createHDLeaf(primaryWallet->walletId(), authPath, { pwdData }
      , dialogData, createAuthLeafCb);
}

std::shared_ptr<bs::sync::hd::SettlementLeaf> bs::sync::WalletsManager::getSettlementLeaf(const bs::Address &addr) const
{
   const auto priWallet = getPrimaryWallet();
   if (!priWallet) {
      logger_->warn("[WalletsManager::getSettlementLeaf] no primary wallet");
      return nullptr;
   }
   const auto group = priWallet->getGroup(bs::hd::BlockSettle_Settlement);
   std::shared_ptr<bs::sync::hd::SettlementLeaf> settlLeaf;
   if (group) {
      const auto settlGroup = std::dynamic_pointer_cast<bs::sync::hd::SettlementGroup>(group);
      if (!settlGroup) {
         logger_->error("[WalletsManager::getSettlementLeaf] wrong settlement group type");
         return nullptr;
      }
      settlLeaf = settlGroup->getLeaf(addr);
   }
   return settlLeaf;
}
#endif   //0

bool bs::sync::WalletsManager::mergeableEntries(const bs::TXEntry &entry1, const bs::TXEntry &entry2) const
{
   if (entry1.txHash != entry2.txHash) {
      return false;
   }
   if (entry1.walletIds == entry2.walletIds) {
      return true;
   }
   WalletPtr wallet1;
   for (const auto &walletId : entry1.walletIds) {
      wallet1 = getWalletById(walletId);
      if (wallet1) {
         break;
      }
   }

   WalletPtr wallet2;
   for (const auto &walletId : entry2.walletIds) {
      wallet2 = getWalletById(walletId);
      if (wallet2) {
         break;
      }
   }

   if (!wallet1 || !wallet2) {
      return false;
   }
   if (wallet1 == wallet2) {
      return true;
   }

   if ((wallet1->type() == bs::core::wallet::Type::Bitcoin)
      && (wallet2->type() == wallet1->type())) {
      const auto rootWallet1 = getHDRootForLeaf(wallet1->walletId());
      const auto rootWallet2 = getHDRootForLeaf(wallet2->walletId());
      if (rootWallet1 == rootWallet2) {
         return true;
      }
   }
   return false;
}

std::vector<bs::TXEntry> bs::sync::WalletsManager::mergeEntries(const std::vector<bs::TXEntry> &entries) const
{
   std::vector<bs::TXEntry> mergedEntries;
   mergedEntries.reserve(entries.size());
   for (const auto &entry : entries) {
      if (mergedEntries.empty()) {
         mergedEntries.push_back(entry);
         continue;
      }
      bool entryMerged = false;
      for (auto &mergedEntry : mergedEntries) {
         if (mergeableEntries(mergedEntry, entry)) {
            entryMerged = true;
            mergedEntry.merge(entry);
            break;
         }
      }
      if (!entryMerged) {
         mergedEntries.push_back(entry);
      }
   }
   return mergedEntries;
}

// assumedRecipientCount is used with CC tests only
bs::core::wallet::TXSignRequest bs::sync::WalletsManager::createPartialTXRequest(uint64_t spendVal
   , const std::map<UTXO, std::string> &inputs, bs::Address changeAddress
   , float feePerByte, uint32_t topHeight
   , const RecipientMap &recipients
   , unsigned changeGroup
   , const Codec_SignerState::SignerState &prevPart, bool useAllInputs
   , unsigned assumedRecipientCount
   , const std::shared_ptr<spdlog::logger> &logger)
{
   if (inputs.empty()) {
      throw std::invalid_argument("No usable UTXOs");
   }
   uint64_t fee = 0;
   uint64_t spendableVal = 0;
   std::vector<UTXO> utxos;
   utxos.reserve(inputs.size());
   for (const auto &input : inputs) {
      utxos.push_back(input.first);
      spendableVal += input.first.getValue();
   }

   bs::CheckRecipSigner prevStateSigner;
   if (prevPart.IsInitialized()) {
      prevStateSigner.deserializeState(prevPart);
   }

   if (feePerByte > 0) {
      size_t baseSize = 0;
      size_t witnessSize = 0;
      for (uint32_t i = 0; i < prevStateSigner.getTxInCount(); ++i) {
         const auto &addr = bs::Address::fromUTXO(prevStateSigner.getSpender(i)->getUtxo());
         baseSize += addr.getInputSize();
         witnessSize += addr.getWitnessDataSize();
      }
      // Optional CC change
      for (const auto &recipients : prevStateSigner.getRecipientMap()) {
         for (const auto &recipient : recipients.second) {
            baseSize += recipient->getSize();
         }
      }
      // CC output, see Recipient_P2WPKH::getSize
      baseSize += 31;
      auto weight = 4 * baseSize + witnessSize;
      uint64_t prevPartTxSize = (weight + 3) / 4;

      try {
         RecipientMap recMap = recipients;

         if (assumedRecipientCount != UINT32_MAX) {
            for (unsigned i=0; i<assumedRecipientCount; i++) {
               uint64_t val = 0;
               if (i==0) {
                  val = spendVal;
               }
               auto rec = std::make_shared<Recipient_P2WPKH>(
                  CryptoPRNG::generateRandom(20), val);
               recMap.emplace(i, std::vector<std::shared_ptr<ScriptRecipient>>({rec}));
            }
         }

         Armory::CoinSelection::PaymentStruct payment(recMap, 0, feePerByte, ADJUST_FEE);
         for (auto &utxo : utxos) {
            const auto scrAddr = bs::Address::fromHash(utxo.getRecipientScrAddr());
            utxo.txinRedeemSizeBytes_ = (unsigned int)scrAddr.getInputSize();
            utxo.witnessDataSizeBytes_ = unsigned(scrAddr.getWitnessDataSize());
            utxo.isInputSW_ = (scrAddr.getWitnessDataSize() != UINT32_MAX);
         }
         payment.addToSize(prevPartTxSize);

         auto coinSelection = Armory::CoinSelection::CoinSelection(nullptr, {}, UINT64_MAX, topHeight);

         Armory::CoinSelection::UtxoSelection selection;
         if (useAllInputs) {
            selection = Armory::CoinSelection::UtxoSelection(utxos);
            selection.fee_byte_ = feePerByte;
            selection.computeSizeAndFee(payment);
         } else {
            selection = coinSelection.getUtxoSelectionForRecipients(payment, utxos);
         }
         fee = selection.fee_;
         utxos = selection.utxoVec_;
      } catch (const std::exception &e) {
         if (logger) {
            SPDLOG_LOGGER_ERROR(logger, "coin selection failed: {}, all inputs will be used", e.what());
         }
      }
   }

   if (utxos.empty()) {
      throw std::logic_error("No UTXOs");
   }

   std::set<std::string> walletIds;
   for (const auto &utxo : utxos) {
      const auto &itInput = inputs.find(utxo);
      if (itInput == inputs.end()) {
         continue;
      }
      walletIds.insert(itInput->second);
   }
   if (walletIds.empty()) {
      throw std::logic_error("No wallet IDs");
   }

   bs::core::wallet::TXSignRequest request;
   request.walletIds.insert(request.walletIds.end(), walletIds.cbegin(), walletIds.cend());
   Signer signer(prevStateSigner);
   signer.setFlags(SCRIPT_VERIFY_SEGWIT);
   request.fee = fee;

   uint64_t inputAmount = 0;
   for (const auto &utxo : utxos) {
      signer.addSpender(std::make_shared<ScriptSpender>(utxo));
      inputAmount += utxo.getValue();
   }
   if (!inputAmount) {
      throw std::logic_error("No inputs detected");
   }

   if (inputAmount < (spendVal + fee)) {
      throw std::overflow_error("Not enough inputs (" + std::to_string(inputAmount)
         + ") to spend " + std::to_string(spendVal + fee));
   }

   for (const auto& group : recipients) {
      for (const auto& recipient : group.second)
      signer.addRecipient(recipient, group.first);
   }

   if (inputAmount > (spendVal + fee)) {
      const bs::XBTAmount::satoshi_type changeVal = inputAmount - (spendVal + fee);
      if (changeAddress.empty()) {
         throw std::invalid_argument("Change address required, but missing");
      }

      signer.addRecipient(
         changeAddress.getRecipient(bs::XBTAmount{ changeVal }),
         changeGroup);
      request.change.value = changeVal;
      request.change.address = changeAddress;
   }

   request.armorySigner_ = signer;
   return request;
}

std::vector<std::string> bs::sync::WalletsManager::getHwWallets(bs::wallet::HardwareEncKey::WalletType walletType,
   std::string deviceId) const
{
   std::vector<std::string> hwWallets;
   for (auto &walletPtr : hdWallets_) {
      if (!walletPtr->isHardwareWallet()) {
         continue;
      }

      bs::wallet::HardwareEncKey key(walletPtr->encryptionKeys()[0]);
      if (key.deviceType() == walletType && key.deviceId() == deviceId) {
         hwWallets.push_back(walletPtr->walletId());
      }
   }

   return hwWallets;
}

std::string bs::sync::WalletsManager::getDefaultSpendWalletId() const
{
   auto walletId = appSettings_->getDefaultWalletId();
   if (walletId.empty()) {
      auto primaryWallet = getPrimaryWallet();
      if (primaryWallet != nullptr) {
         walletId = primaryWallet->walletId();
      }
   }

   return walletId;
}
