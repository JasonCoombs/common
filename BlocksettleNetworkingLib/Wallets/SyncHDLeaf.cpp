/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SyncHDLeaf.h"

#include "AddressValidationState.h"
#include "CheckRecipSigner.h"
#include "FastLock.h"
#include "WalletSignerContainer.h"
#include "WalletUtils.h"

#include <unordered_map>
#include <spdlog/spdlog.h>
#include <QLocale>
#include <QMutexLocker>

// BST-2747: Require 1 conf for external addresses too
const uint32_t kExtConfCount = 1;
const uint32_t kIntConfCount = 1;
static const std::string& kScanSuffix{ ".scan" };

using namespace Armory::Wallets;
using namespace bs::sync;

hd::Leaf::Leaf(const std::string &walletId, const std::string &name, const std::string &desc
   , WalletSignerContainer *container, const std::shared_ptr<spdlog::logger> &logger
   , bs::core::wallet::Type type, bool extOnlyAddresses)
   : bs::sync::Wallet(container, logger)
   , walletId_(walletId)
   , type_(type)
   , name_(name)
   , desc_(desc)
   , isExtOnly_(extOnlyAddresses)
{}

hd::Leaf::~Leaf()
{
   validityFlag_.reset();
}

void hd::Leaf::synchronize(const std::function<void()> &cbDone)
{
   const auto &cbProcess = [this, cbDone, handle = validityFlag_.handle()]
      (bs::sync::WalletData data) mutable
   {
      // Do not lock handle here as this could cause deadlocks!
      // hd::Leaf is destroyed and this callback is called on same thread (main thread) and so it's OK.
      if (!handle.isValid()) {
         return;
      }
      reset();

      if (wct_) {
         wct_->metadataChanged(walletId());
      }

      if (data.highestExtIndex == UINT32_MAX ||
         data.highestIntIndex == UINT32_MAX)
         throw WalletException("unintialized addr chain use index");

      lastExtIdx_ = data.highestExtIndex;
      lastIntIdx_ = data.highestIntIndex;

      logger_->debug("[sync::hd::Leaf::synchronize] {}: last indices {}+{}={} address[es]"
         , walletId(), lastExtIdx_, lastIntIdx_, data.addresses.size());
      for (const auto &addr : data.addresses) {
         addAddress(addr.address, addr.index, false);
         setAddressComment(addr.address, addr.comment, false);
      }

      {
         FastLock locker{addressPoolLock_};
         for (const auto &addr : data.addrPool) {
            //addPool normally won't contain comments
            const auto path = bs::hd::Path::fromString(addr.index);
            auto type = path.get(-2);
            auto index = path.get(-1);
            addressPool_[{ path }] = addr.address;
            poolByAddr_[addr.address] = { path };
            if (type == addrTypeExternal) {
               lastPoolExtIdx_ = std::max(lastPoolExtIdx_, index);
            } else {
               lastPoolIntIdx_ = std::max(lastPoolIntIdx_, index);
            }
         }
      }

      for (const auto &txComment : data.txComments) {
         setTransactionComment(txComment.txHash, txComment.comment, false);
      }
      if (cbDone) {
         cbDone();
      }
   };

   signContainer_->syncWallet(walletId(), cbProcess);
}

void hd::Leaf::setPath(const bs::hd::Path &path)
{
   if (path != path_) {
      path_ = path;
      suffix_.clear();
      suffix_ = bs::hd::Path::elemToKey(index());
      walletName_ = name_ + "/" + suffix_;
   }

   if (!path.length()) {
      reset();
   }
}

std::vector<std::string> hd::Leaf::internalIds() const
{
   std::vector<std::string> result{ walletId() };
   if (!isExtOnly_) {
      result.push_back(walletIdInt());
   }
   return result;
}

void hd::Leaf::onRefresh(const std::vector<BinaryData> &ids, bool online)
{
   auto cbRegistered = [this] {
      isRegistered_ = Registered::Registered;
      onRegistrationCompleted();
   };
   auto cbRegisterExt = [this, online, cbRegistered] () {
      if (isExtOnly_ || (regIdExt_.empty() && regIdInt_.empty())) {
         cbRegistered();
         if (online) {
            postOnline();
         }
      }
   };
   auto cbRegisterInt = [this, online, cbRegistered] () {
      if (regIdExt_.empty() && regIdInt_.empty()) {
         cbRegistered();
         if (online) {
            postOnline();
         }
      }
   };

   {
      std::unique_lock<std::mutex> lock(regMutex_);
      if (!regIdExt_.empty() || !regIdInt_.empty()) {
         for (const auto &id : ids) {
            if (id.empty()) {
               continue;
            }
            logger_->debug("[sync::hd::Leaf::onRefresh] {}: id={}, extId={}, intId={}", walletId()
               , id.toBinStr(), regIdExt_, regIdInt_);
            if (id == BinaryData::fromString(regIdExt_)) {
               regIdExt_.clear();
               cbRegisterExt();
            } else if (id == BinaryData::fromString(regIdInt_)) {
               regIdInt_.clear();
               cbRegisterInt();
            }
         }
      }

      if (!refreshCallbacks_.empty()) {
         for (const auto &id : ids) {
            const auto &it = refreshCallbacks_.find(id.toBinStr());
            if (it != refreshCallbacks_.end()) {
               it->second();
               refreshCallbacks_.erase(it);
            }
         }
      }
   }

   std::lock_guard<std::mutex> lock(regMutex_);
   if (!unconfTgtRegIds_.empty()) {
      for (const auto &id : ids) {
         const auto it = std::find(unconfTgtRegIds_.cbegin(), unconfTgtRegIds_.cend(), id.toBinStr());
         if (it != unconfTgtRegIds_.end()) {
            unconfTgtRegIds_.erase(it);
         }
      }

      if (unconfTgtRegIds_.empty()) {
         bs::sync::Wallet::init();
      }
   }
}

void hd::Leaf::onRegistered()
{
   isRegistered_ = Registered::Registered;

   //TODO: process refreshCallbacks if needed
}

std::vector<std::string> hd::Leaf::setUnconfirmedTarget()
{
   std::vector<std::string> regIDs;
   try {
      if (btcWallet_) {
         regIDs.push_back(btcWallet_->setUnconfirmedTarget(kExtConfCount));
      }
      if (btcWalletInt_) {
         regIDs.push_back(btcWalletInt_->setUnconfirmedTarget(kIntConfCount));
      }
   }
   catch (const LWS_Error &e) {
      logger_->error("[hd::Leaf::setUnconfirmedTarget] LWS error: {}", e.what());
      return {};
   }
   return regIDs;
}

void hd::Leaf::postOnline(bool force)
{
   if ((skipPostOnline_ || firstInit_) && !force) {
      return;
   }
   if (!armory_ || (armory_->state() == ArmoryState::Offline)) {
      logger_->error("[hd::Leaf::postOnline] Armory is offline");
      return;
   }

   unconfTgtRegIds_ = setUnconfirmedTarget();
   if (unconfTgtRegIds_.empty()) {
      logger_->error("[hd::Leaf::postOnline] failed to set unconf target[s]");
      return;
   }

   const auto &cbTrackAddrChain = [this, handle = validityFlag_.handle()](bs::sync::SyncState st) mutable {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }

      if (st != bs::sync::SyncState::Success) {
         updateBalances();
         if (wct_) {
            wct_->walletReady(walletId());
         }
         return;
      }
      synchronize([this, handle] () mutable {
         ValidityGuard lock(handle);
         if (!handle.isValid()) {
            return;
         }
         updateBalances();
         if (!isExtOnly_ && lastPoolIntIdx_ < lastIntIdx_ + intAddressPoolSize_) {
            SPDLOG_LOGGER_DEBUG(logger_, "top up internal addr pool for {}, pool size: {}, used addr size: {}"
               , walletId(), lastPoolIntIdx_ + 1, lastIntIdx_ + 1);
            topUpAddressPool(false, [this, handle] () mutable {
               ValidityGuard lock(handle);
               if (!handle.isValid()) {
                  return;
               }
               if (wct_) {
                  wct_->balanceUpdated(walletId());
               }
               postOnline(true);
            });
            return;
         }
         if (lastPoolExtIdx_ < lastExtIdx_ + extAddressPoolSize_) {
            SPDLOG_LOGGER_DEBUG(logger_, "top up external addr pool for {}, pool size: {}, used addr size: {}"
               , walletId(), lastPoolExtIdx_ + 1, lastExtIdx_ + 1);
            topUpAddressPool(true, [this, handle] () mutable {
               ValidityGuard lock(handle);
               if (!handle.isValid()) {
                  return;
               }
               if (wct_) {
                  wct_->balanceUpdated(walletId());
               }
               postOnline(true);
            });
            return;
         }
         if (wct_) {
            wct_->walletReady(walletId());
         }
      });
   };
   const bool rc = getAddressTxnCounts([this, cbTrackAddrChain, handle = validityFlag_.handle()] () mutable {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }
      trackChainAddressUse(cbTrackAddrChain);
   });
   if (!rc) {
      SPDLOG_LOGGER_ERROR(logger_, "getAddressTxnCounts failed");
   }
}

void hd::Leaf::init(bool force)
{
   if (firstInit_ && !force)
      return;

   if (!armory_ || (armory_->state() != ArmoryState::Ready)) {
      return;
   }
   postOnline();

   if (firstInit_ && force) {
      bs::sync::Wallet::init(force);
   }
}

void hd::Leaf::reset()
{
   std::lock_guard<std::mutex> lock(regMutex_);
   FastLock locker{addressPoolLock_};

   lastIntIdx_ = lastExtIdx_ = 0;
   usedAddresses_.clear();
   intAddresses_.clear();
   extAddresses_.clear();
   addrToIndex_.clear();
   addrPrefixedHashes_.clear();
   addressPool_.clear();
   poolByAddr_.clear();
   if (wct_) {
      wct_->walletReset(walletId());
   }
   unconfTgtRegIds_.clear();
}

std::string hd::Leaf::walletId() const
{
   return walletId_;
}

std::string hd::Leaf::walletIdInt() const
{
   if (isExtOnly_)
      throw std::runtime_error("not internal chain");

   if (walletIdInt_.empty()) {
      for (const auto &c : walletId()) {
         if (isupper(c)) {
            walletIdInt_.push_back(tolower(c));
         }
         else if (islower(c)) {
            walletIdInt_.push_back(toupper(c));
         }
         else {
            walletIdInt_.push_back(c);
         }
      }
   }
   return walletIdInt_;
}

std::string bs::sync::hd::Leaf::walletScanId() const
{
   return walletId() + kScanSuffix;
}

std::string bs::sync::hd::Leaf::walletScanIdInt() const
{
   return walletIdInt() + kScanSuffix;
}

std::string hd::Leaf::description() const
{
   return desc_;
}

std::string hd::Leaf::shortName() const
{
   std::string name;
   switch (static_cast<bs::hd::Purpose>(path_.get(0) & ~bs::hd::hardFlag)) {
   case bs::hd::Purpose::Native:
      name = QObject::tr("Native SegWit").toStdString();
      break;
   case bs::hd::Purpose::Nested:
      name = QObject::tr("Nested SegWit").toStdString();
      break;
   case bs::hd::Purpose::NonSegWit:
      name = QObject::tr("Legacy").toStdString();
      break;
   case bs::hd::Purpose::Virtual:
      name = QObject::tr("Armory").toStdString();
      break;
   default:
      name = QObject::tr("Unknown").toStdString();
      break;
   }
   name += " " + suffix_;
   return name;
}

bool hd::Leaf::containsAddress(const bs::Address &addr)
{
   return !getAddressIndex(addr).empty();
}

bool hd::Leaf::containsHiddenAddress(const bs::Address &addr) const
{
   FastLock locker{addressPoolLock_};
   return (poolByAddr_.find(addr) != poolByAddr_.end());
}

std::vector<std::pair<bs::Address, std::string>> bs::sync::hd::Leaf::getAddressPool() const
{
   std::vector<std::pair<bs::Address, std::string>> result;
   result.reserve(addressPool_.size());
   for (const auto& poolPair : addressPool_) {
      result.push_back({ poolPair.second, poolPair.first.path.toString() });
   }
   return result;
}

size_t hd::Leaf::getAddressPoolSize() const
{
   FastLock locker{addressPoolLock_};
   return addressPool_.size();
}

// Return an external-facing address.
void hd::Leaf::getNewExtAddress(const CbAddress &cb)
{
   createAddress(cb, false);
}

// Return an internal-facing address.
void hd::Leaf::getNewIntAddress(const CbAddress &cb)
{
   createAddress(cb, true);
}

// Return a change address.
void hd::Leaf::getNewChangeAddress(const CbAddress &cb)
{
   createAddress(cb, isExtOnly_ ? false : true);
}

std::vector<BinaryData> hd::Leaf::getAddrHashes() const
{
   std::vector<BinaryData> result;

   const auto addrsExt = getAddrHashesExt();
   const auto addrsInt = getAddrHashesInt();

   result.insert(result.end(), addrsExt.cbegin(), addrsExt.cend());
   result.insert(result.end(), addrsInt.cbegin(), addrsInt.cend());

   return result;
}

std::vector<BinaryData> hd::Leaf::getAddrHashesExt() const
{
   auto result = addrPrefixedHashes_.external;
   {
      FastLock locker{addressPoolLock_};
      for (const auto &addr : addressPool_) {
         if (addr.first.path.get(-2) == addrTypeExternal) {
            result.insert(addr.second.id());
         }
      }
   }
   return std::vector<BinaryData>(result.begin(), result.end());
}

std::vector<BinaryData> hd::Leaf::getAddrHashesInt() const
{
   auto result = addrPrefixedHashes_.internal;
   {
      FastLock locker{addressPoolLock_};
      for (const auto &addr : addressPool_) {
         if (addr.first.path.get(-2) == addrTypeInternal) {
            result.insert(addr.second.id());
         }
      }
   }
   return std::vector<BinaryData>(result.begin(), result.end());
}

#if 0
std::vector<std::string> hd::Leaf::registerWallet(
   const std::shared_ptr<ArmoryConnection> &armory, bool asNew)
{
   setArmory(armory);

   if (armory_) {
      firstInit_ = false;
      isRegistered_ = (isRegistered_ == Registered::Registered) ? Registered::Updating : Registered::Offline;
      const auto addrsExt = getAddrHashesExt();
      const auto addrsInt = isExtOnly_ ? std::vector<BinaryData>{} : getAddrHashesInt();
      std::vector<std::string> regIds;

      std::unique_lock<std::mutex> lock(regMutex_);
      btcWallet_ = armory_->instantiateWallet(walletId());
      if (!btcWallet_) {
         return {};
      }
      regIdExt_ = btcWallet_->registerAddresses(addrsExt, asNew);
      registeredAddresses_.insert(addrsExt.begin(), addrsExt.end());
      regIds.push_back(regIdExt_);

      if (!isExtOnly_) {
         btcWalletInt_ = armory_->instantiateWallet(walletIdInt());
         regIdInt_ = btcWalletInt_->registerAddresses(addrsInt, asNew);
         registeredAddresses_.insert(addrsInt.begin(), addrsInt.end());
         regIds.push_back(regIdInt_);
      }
      logger_->debug("[sync::hd::Leaf::registerWallet] registered {}+{} addresses in {}, {} regIds {} {}"
         , addrsExt.size(), addrsInt.size(), walletId(), regIds.size()
         , regIdExt_, regIdInt_);
      return regIds;
   }
   return {};
}

void hd::Leaf::unregisterWallet()
{
   // Check armory state before unregister call as armory will throw LWS_Error if socket connection is offline
   if (armory_ && armory_->state() == ArmoryState::Ready) {
      if (btcWallet_) {
         btcWallet_->unregister();
      }
      if (btcWalletInt_) {
         btcWalletInt_->unregister();
      }
   }
   bs::sync::Wallet::unregisterWallet();
}
#endif //0

Wallet::WalletRegData hd::Leaf::regData() const
{
   const auto addrsExt = getAddrHashesExt();
   const auto addrsInt = isExtOnly_ ? std::vector<BinaryData>{} : getAddrHashesInt();
   WalletRegData result;
   result[walletId()] = addrsExt;
   registeredAddresses_.insert(addrsExt.begin(), addrsExt.end());

   if (!isExtOnly_) {
      result[walletIdInt()] = addrsInt;
      registeredAddresses_.insert(addrsInt.begin(), addrsInt.end());
   }
   logger_->debug("[sync::hd::Leaf::regData] {}+{} addresses in {}"
      , addrsExt.size(), addrsInt.size(), walletId());
   return result;
}

Wallet::UnconfTgtData hd::Leaf::unconfTargets() const
{
   return isExtOnly_ ? Wallet::UnconfTgtData{ { walletId(), 1 } }
   : Wallet::UnconfTgtData{ { walletId(), 1 }, { walletIdInt(), 1 } };
}

void hd::Leaf::createAddress(const CbAddress &cb, bool isInternal)
{
   bs::hd::Path addrPath;
   if (isInternal && !isExtOnly_) {
      addrPath.append(addrTypeInternal);
      addrPath.append(lastIntIdx_++);
   }
   else {
      addrPath.append(addrTypeExternal);
      addrPath.append(lastExtIdx_++);
   }
   createAddress(cb, { addrPath });
}

void hd::Leaf::createAddress(const CbAddress &cb, const AddrPoolKey &key)
{
   const auto &swapKey = [this, key](void) -> bs::Address
   {
      bs::Address result;

      FastLock locker{addressPoolLock_};
      const auto addrPoolIt = addressPool_.find(key);

      if (addrPoolIt != addressPool_.end()) {
         result = std::move(addrPoolIt->second);
         addressPool_.erase(addrPoolIt->first);
         poolByAddr_.erase(result);
      }
      return result;
   };

   const auto &cbAddAddr = [this, cb, key](const bs::Address &addr) {
      addAddress(addr, key.path.toString());
      if (cb) {
         cb(addr);
      }
      if (wct_) {
         wct_->addressAdded(walletId());
      }
   };

   const auto result = swapKey();
   if (result.empty()) {
      auto topUpCb = [this, key, swapKey, cbAddAddr, cb](void)
      {
         const auto result = swapKey();
         if (result.empty()) {
            logger_->error("[{}] failed to find {} after topping up the pool"
               , __func__, key.path.toString());
            cb(result);
         }
         else {
            cbAddAddr(result);
         }
      };

      const bool extInt = (key.path.get(-2) == addrTypeExternal) ? true : false;
      topUpAddressPool(extInt, topUpCb);
   }
   else {
      const std::set<BinaryData> addrSet = { result.id() };
      signContainer_->syncAddressBatch(walletId(), addrSet, [](bs::sync::SyncState) {});
      cbAddAddr(result);
   }
}

void hd::Leaf::topUpAddressPool(bool extInt, const std::function<void()> &cb)
{
   if (!signContainer_) {
      logger_->error("[sync::hd::Leaf::topUpAddressPool] uninited signer container");
      throw std::runtime_error("uninitialized sign container");
   }

   auto fillUpAddressPoolCallback = [this, extInt, cb, handle = validityFlag_.handle()](
      const std::vector<std::pair<bs::Address, std::string>>& addrVec) mutable
   {
      // No need to lock handle as callback is called on main thread
      if (!handle.isValid()) {
         return;
      }

      /***
      This lambda adds the newly generated addresses to the address pool.

      New addresses generated by topUpAddressPool are not instantiated yet,
      they are only of use for registering the underlying script hashes
      with the DB, which is why they are only saved in the pool.
      ***/

      for (const auto &addrPair : addrVec) {
         const auto path = bs::hd::Path::fromString(addrPair.second);

         FastLock locker{addressPoolLock_};
         addressPool_[{ path }] = addrPair.first;
         poolByAddr_[addrPair.first] = { path };
      }

      //register new addresses with db
      if (armory_) {
         std::vector<BinaryData> addrHashes;
         for (auto& addrPair : addrVec) {
            addrHashes.push_back(addrPair.first.prefixed());
         }

         if (extInt) {
            std::unique_lock<std::mutex> lock(regMutex_);
            const auto regId = btcWallet_->registerAddresses(addrHashes, true);
            refreshCallbacks_[regId] = cb;
         }
         else {
            std::unique_lock<std::mutex> lock(regMutex_);
            const auto regId = btcWalletInt_->registerAddresses(addrHashes, true);
            refreshCallbacks_[regId] = cb;
         }
         registeredAddresses_.insert(addrHashes.begin(), addrHashes.end());
         return;
      }

      if (cb) {
         cb();
      }
   };

   const unsigned int lookup = extInt ? extAddressPoolSize_ : intAddressPoolSize_;
   signContainer_->extendAddressChain(
      walletId(), lookup, extInt, fillUpAddressPoolCallback);
}

hd::Leaf::AddrPoolKey hd::Leaf::getAddressIndexForAddr(const BinaryData &addr) const
{
   const auto itIndex = addrToIndex_.find(addr);
   if (itIndex != addrToIndex_.end()) {
      return itIndex->second;
   }
   return AddrPoolKey();
}

hd::Leaf::AddrPoolKey hd::Leaf::addressIndex(const bs::Address &addr) const
{
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   const auto itIndex = addrToIndex_.find(addr.unprefixed());
   if (itIndex == addrToIndex_.end()) {
      return {};
   }
   return itIndex->second;
}

bs::hd::Path hd::Leaf::getPathForAddress(const bs::Address &addr) const
{
   FastLock locker{addressPoolLock_};
   const auto index = addressIndex(addr);
   if (index.empty()) {
      const auto &itPool = poolByAddr_.find(addr);
      if (itPool == poolByAddr_.end()) {
         return {};
      }
      return itPool->second.path;
   }
   if (index.path.length() < 2) {
      return {};
   }
   return index.path;
}

std::shared_ptr<Armory::Signer::ResolverFeed> hd::Leaf::getPublicResolver() const
{
   return nullptr;
}

#if 0
bool hd::Leaf::getLedgerDelegateForAddress(const bs::Address &addr
   , const std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)> &cb)
{
   if (!armory_) {
      return false;
   }
   {
      std::unique_lock<std::mutex> lock(balanceData_->cbMutex);
      const auto &itCb = cbLedgerByAddr_.find(addr);
      if (itCb != cbLedgerByAddr_.end()) {
         logger_->error("[sync::hd::Leaf::getLedgerDelegateForAddress] ledger callback for addr {} already exists", addr.display());
         return false;
      }
      cbLedgerByAddr_[addr] = cb;
   }

   const auto path = getPathForAddress(addr);
   if (path.get(-2) == addrTypeInternal) {
      return armory_->getLedgerDelegateForAddress(walletIdInt(), addr);
   }
   else {
      return armory_->getLedgerDelegateForAddress(walletId(), addr);
   }
}
#endif   //0

bool hd::Leaf::hasId(const std::string &id) const
{
   return ((walletId() == id) || (!isExtOnly_ && (walletIdInt() == id)));
}

bool bs::sync::hd::Leaf::hasScanId(const std::string& id) const
{
   return ((walletScanId() == id) || (!isExtOnly_ && (walletScanIdInt() == id)));
}

int hd::Leaf::addAddress(const bs::Address &addr, const std::string &index, bool sync)
{
   const auto path = bs::hd::Path::fromString(index);
   const bool isInternal = (path.get(-2) == addrTypeInternal);
   const int id = bs::sync::Wallet::addAddress(addr, index, sync);
   const auto addrIndex = path.get(-1);
   if (isInternal) {
      intAddresses_.push_back(addr);
      addrPrefixedHashes_.internal.insert(addr.id());
      if (addrIndex >= lastIntIdx_) {
         lastIntIdx_ = addrIndex + 1;
      }
   } else {
      extAddresses_.push_back(addr);
      addrPrefixedHashes_.external.insert(addr.id());
      if (addrIndex >= lastExtIdx_) {
         lastExtIdx_ = addrIndex + 1;
      }
   }
   std::lock_guard<std::recursive_mutex> lock(mutex_);
   addrToIndex_[addr.unprefixed()] = {path};
   return id;
}

bs::hd::Purpose hd::Leaf::purpose() const
{
   return static_cast<bs::hd::Purpose>(path().get(0) & ~bs::hd::hardFlag);
}

bool hd::Leaf::getSpendableTxOutList(const ArmoryConnection::UTXOsCb &cb, uint64_t val, bool excludeReservation)
{  // process the UTXOs for the purposes of handling internal/external addresses
   const ArmoryConnection::UTXOsCb &cbWrap = [this, cb, val](const std::vector<UTXO> &utxos) {
      std::vector<UTXO> filteredUTXOs;
      incompleteUTXOs_.clear();
      for (const auto &utxo : utxos) {
         const auto nbConf = armory_->getConfirmationsNumber(utxo.getHeight());
         const auto addr = bs::Address::fromUTXO(utxo);
         const auto confCutOff = isExternalAddress(addr) ? kExtConfCount : kIntConfCount;
         if (nbConf >= confCutOff) {
            filteredUTXOs.emplace_back(std::move(utxo));
         }
         else {
            incompleteUTXOs_.emplace_back(std::move(utxo));
         }
      }
      if (cb) {
         cb(bs::selectUtxoForAmount(filteredUTXOs, val));
      }
   };
   return bs::sync::Wallet::getSpendableTxOutList(cbWrap, std::numeric_limits<uint64_t>::max(), excludeReservation);
}

std::vector<UTXO> hd::Leaf::getIncompleteUTXOs() const
{
   auto result = bs::sync::Wallet::getIncompleteUTXOs();
   result.insert(result.cend(), incompleteUTXOs_.cbegin(), incompleteUTXOs_.cend());
   return result;
}

BTCNumericTypes::balance_type hd::Leaf::getSpendableBalance() const
{
   return (Wallet::getSpendableBalance() - spendableBalanceCorrection_);
}

bool hd::Leaf::getHistoryPage(uint32_t id, std::function<void(const Wallet *wallet
   , std::vector<DBClientClasses::LedgerEntry>)> cb, bool onlyNew) const
{
   auto cbCnt = std::make_shared<std::atomic_uint>(0);
   auto result = std::make_shared<std::vector<DBClientClasses::LedgerEntry>>();
   const auto &cbWrap = [this, cb, cbCnt, result](const Wallet *wallet
      , std::vector<DBClientClasses::LedgerEntry> entries) {
      result->insert(result->end(), entries.begin(), entries.end());
      if (isExtOnly_ || (cbCnt->fetch_add(1) > 0)) {
         cb(wallet, *result);
      }
   };
   bool rc = Wallet::getHistoryPage(btcWallet_, id, cbWrap, onlyNew);
   if (!isExtOnly_) {
      rc &= Wallet::getHistoryPage(btcWalletInt_, id, cbWrap, onlyNew);
   }
   return rc;
}

std::string hd::Leaf::getAddressIndex(const bs::Address &addr)
{
   const auto path = getPathForAddress(addr);
   if (path.length()) {
      return path.toString();
   }
   return {};
}

std::string hd::Leaf::getWalletIdForAddress(const bs::Address &addr) const
{
   if (isExternalAddress(addr)) {
      return walletId();
   }
   else if (!isExtOnly_) {
      return walletIdInt();
   }
   return {};
}

bool hd::Leaf::isExternalAddress(const bs::Address &addr) const
{
   const auto &path = getPathForAddress(addr);
   if (path.length() < 2) {
      return false;
   }
   return (path.get(-2) == addrTypeExternal);
}

void hd::Leaf::merge(const std::shared_ptr<Wallet> &walletPtr)
{
   bs::sync::Wallet::merge(walletPtr);

   //rudimentary implementation, flesh it out on the go
   auto leafPtr = std::dynamic_pointer_cast<hd::Leaf>(walletPtr);
   if (leafPtr == nullptr) {
      throw std::runtime_error("sync::Wallet child class mismatch");
   }
   addrComments_.insert(
      leafPtr->addrComments_.begin(), leafPtr->addrComments_.end());
   txComments_.insert(
      leafPtr->txComments_.begin(), leafPtr->txComments_.end());

   {
      FastLock locker{addressPoolLock_};
      addressPool_ = leafPtr->addressPool_;
      poolByAddr_ = leafPtr->poolByAddr_;
   }

   intAddresses_ = leafPtr->intAddresses_;
   extAddresses_ = leafPtr->extAddresses_;

   lastIntIdx_ = leafPtr->lastIntIdx_;
   lastExtIdx_ = leafPtr->lastExtIdx_;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

hd::XBTLeaf::XBTLeaf(const std::string &walletId, const std::string &name, const std::string &desc
   , WalletSignerContainer *container,const std::shared_ptr<spdlog::logger> &logger, bool extOnlyAddresses)
   : Leaf(walletId, name, desc, container, logger, bs::core::wallet::Type::Bitcoin, extOnlyAddresses)
{
}

hd::XBTLeaf::~XBTLeaf()
{
   validityFlag_.reset();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0
hd::AuthLeaf::AuthLeaf(const std::string &walletId, const std::string &name, const std::string &desc
   , WalletSignerContainer *container,const std::shared_ptr<spdlog::logger> &logger)
   : Leaf(walletId, name, desc, container, logger, bs::core::wallet::Type::Authentication, true)
{
   intAddressPoolSize_ = 0;
   extAddressPoolSize_ = 5;
}

hd::AuthLeaf::~AuthLeaf()
{
   validityFlag_.reset();
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

hd::CCLeaf::CCLeaf(const std::string &walletId, const std::string &name, const std::string &desc
   , WalletSignerContainer *container, const std::shared_ptr<spdlog::logger> &logger)
   : hd::Leaf(walletId, name, desc, container, logger, bs::core::wallet::Type::ColorCoin, true)
{}

hd::CCLeaf::~CCLeaf()
{
   validityFlag_.reset();
}

void hd::CCLeaf::setCCDataResolver(const std::shared_ptr<CCDataResolver> &resolver)
{
   assert(resolver != nullptr);
   ccResolver_ = resolver;
   setPath(path_);
   lotSize_ = ccResolver_->lotSizeFor(suffix_);
}

void hd::CCLeaf::setCCTracker(const std::shared_ptr<ColoredCoinTrackerClient> &tracker)
{
   assert(tracker != nullptr);
   tracker_ = tracker;
}

void hd::CCLeaf::setPath(const bs::hd::Path &path)
{
   hd::Leaf::setPath(path);
   if ((path_.length() > 0) && ccResolver_) {
      suffix_ = ccResolver_->nameByWalletIndex(static_cast<bs::hd::Path::Elem>(path_.get(-1)));
      if (!suffix_.empty()) {
         walletName_ = name_ + "/" + suffix_;
      }
   }
}

void hd::CCLeaf::setArmory(const std::shared_ptr<ArmoryConnection> &armory)
{
   hd::Leaf::setArmory(armory);
   if (sync::Wallet::armory_) {
      if (!act_) {
         act_ = make_unique<CCWalletACT>(this);
         act_->init(armory.get());
      }
   }
}

std::vector<std::string> hd::CCLeaf::setUnconfirmedTarget()
{
   if (!btcWallet_) {
      return {};
   }
   try {
      return { btcWallet_->setUnconfirmedTarget(kIntConfCount) };
   }
   catch (const LWS_Error &) {
      return {};
   }
}

std::map<BinaryData, std::set<unsigned>> hd::CCLeaf::getOutpointMapFromTracker(bool withZC) const
{
   if (!tracker_) {
      return {};
   }
   const auto &addrSet = collectAddresses();
   auto outpointMap = tracker_->getCCUtxoForAddresses(addrSet, withZC);
   return outpointMap;
}

bool hd::CCLeaf::getSpendableTxOutList(const ArmoryConnection::UTXOsCb &cb, uint64_t val, bool excludeReservation)
{
   if (!armory_) {
      return false;
   }
   const auto cbWrap = [this, cb, val, handle = validityFlag_.handle(), excludeReservation]
      (const std::vector<UTXO> &utxos, std::exception_ptr eptr) mutable
   {
      ValidityGuard lock(handle);
      if (!handle.isValid()) {
         return;
      }
      std::vector<UTXO> filteredUTXOs;
      incompleteUTXOs_.clear();
      for (const auto &utxo : utxos) {
         const auto nbConf = armory_->getConfirmationsNumber(utxo.getHeight());
         if (nbConf >= kIntConfCount) {
            filteredUTXOs.emplace_back(std::move(utxo));
         }
         else {
            incompleteUTXOs_.emplace_back(std::move(utxo));
         }
      }
      if (UtxoReservation::instance() && excludeReservation) {
         UtxoReservation::instance()->filter(filteredUTXOs, reservedUTXOs_);
      }
      if (cb) {
         cb(bs::selectUtxoForAmount(std::move(filteredUTXOs), val));
      }
   };

   if (tracker_ == nullptr) {
      if (ccResolver_->genesisAddrFor(suffix_).empty()) {
         return bs::sync::hd::Leaf::getSpendableTxOutList(cb, val, excludeReservation);
      }
      // GA is null if this CC leaf created inside PB and contain real GA address
      // if it is not - tracker should be set
      return false;
   }

   const auto &outpointMap = getOutpointMapFromTracker(false);
   return armory_->getOutputsForOutpoints(outpointMap, false, cbWrap);
}

void hd::CCLeaf::CCWalletACT::onStateChanged(ArmoryState state)
{
   if (state == ArmoryState::Ready) {
      parent_->init(true);
   }
}

bool hd::CCLeaf::getSpendableZCList(const ArmoryConnection::UTXOsCb &cb) const
{
   if (!armory_) {
      return false;
   }
   const auto cbWrap = [cb, armory = armory_]
      (const std::vector<UTXO> &utxos, std::exception_ptr eptr)
   {
      std::vector<UTXO> filteredUTXOs;
      for (const auto &utxo : utxos) {
         const auto nbConf = armory->getConfirmationsNumber(utxo.getHeight());
         if (nbConf == 0) {
            filteredUTXOs.emplace_back(std::move(utxo));
         }
      }
      if (cb) {
         cb(filteredUTXOs);
      }
   };

   if (tracker_ == nullptr) {
      if (ccResolver_->genesisAddrFor(suffix_).empty()) {
         return bs::sync::hd::Leaf::getSpendableZCList(cb);
      }
      // GA is null if this CC leaf created inside PB and contain real GA address
      // if it is not - tracker should be set
      return false;
   }

   const auto &outpointMap = getOutpointMapFromTracker(true);
   return armory_->getOutputsForOutpoints(outpointMap, true, cbWrap);
}

std::set<BinaryData> hd::CCLeaf::collectAddresses() const
{
   std::set<BinaryData> result;
   for (const auto &addr : getUsedAddressList()) {
      result.insert(addr.id());
   }
   return result;
}

bool hd::CCLeaf::isBalanceAvailable() const
{
   return lotSize_ ? hd::Leaf::isBalanceAvailable() : false;
}

BTCNumericTypes::balance_type hd::CCLeaf::getSpendableBalance() const
{
   if (!tracker_ || !lotSize_) {
      return -1;
   }
   return tracker_->getConfirmedCcValueForAddresses(collectAddresses()) / lotSize_;
}

BTCNumericTypes::balance_type hd::CCLeaf::getUnconfirmedBalance() const
{
   if (!tracker_ || !lotSize_) {
      return -1;
   }
   return tracker_->getUnconfirmedCcValueForAddresses(collectAddresses()) / lotSize_;
}

BTCNumericTypes::balance_type hd::CCLeaf::getTotalBalance() const
{
   if (!tracker_ || !lotSize_) {
      return -1;
   }
   return (getUnconfirmedBalance() + getSpendableBalance());
}

std::vector<uint64_t> hd::CCLeaf::getAddrBalance(const bs::Address &addr) const
{
   if (!tracker_ || !lotSize_) {
      return {};
   }
   return { tracker_->getCcValueForAddress(addr.id()) / lotSize_
      , tracker_->getUnconfirmedCcValueForAddresses({ addr.id() } ) / lotSize_
      , tracker_->getConfirmedCcValueForAddresses({ addr.id() }) / lotSize_ };
}

TxValidity hd::CCLeaf::isTxValid(const BinaryData &txHash) const
{
   if (!tracker_) {
      return TxValidity::Unknown;
   }
   if (tracker_->isTxHashValidHistory(txHash)) {
      return TxValidity::Valid;
   }
   // Can't say for sure until tracker is ready, mark as unknown for now
   if (!tracker_->ready()) {
      return TxValidity::Unknown;
   }
   return TxValidity::Invalid;
}

BTCNumericTypes::balance_type hd::CCLeaf::getTxBalance(int64_t val) const
{
   if (lotSize_ == 0) {
      return val / BTCNumericTypes::BalanceDivider;
   }
   return (double)val / lotSize_;
}

QString hd::CCLeaf::displayTxValue(int64_t val) const
{
   return QLocale().toString(getTxBalance(val), 'f', 0);
}

QString hd::CCLeaf::displaySymbol() const
{
   return suffix_.empty() ? hd::Leaf::displaySymbol() : QString::fromStdString(suffix_);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

hd::SettlementLeaf::SettlementLeaf(const std::string &walletId, const std::string &name, const std::string &desc
   , WalletSignerContainer *container, const std::shared_ptr<spdlog::logger> &logger)
   : Leaf(walletId, name, desc, container, logger, bs::core::wallet::Type::Settlement, true)
{
   intAddressPoolSize_ = 0;
   extAddressPoolSize_ = 0;
}

hd::SettlementLeaf::~SettlementLeaf()
{
   validityFlag_.reset();
}

void hd::SettlementLeaf::createAddress(const CbAddress &cb, const AddrPoolKey &key)
{
   throw std::runtime_error("Settlement leaves do not yield addresses");
}

void hd::SettlementLeaf::topUpAddressPool(bool extInt, const std::function<void()> &cb)
{
   throw std::runtime_error("Settlement leaves do not yield addresses");
}

void hd::SettlementLeaf::setSettlementID(const SecureBinaryData& id
   , const std::function<void(bool)> &cb)
{
   if (signContainer_ == nullptr) {
      if (cb)
         cb(false);
   }

   signContainer_->setSettlementID(walletId(), id, cb);
}

void hd::SettlementLeaf::getRootPubkey(const std::function<void(const SecureBinaryData &)> &cb) const
{
   if (signContainer_ == nullptr) {
      if (cb) {
         cb({});
      }
      return;
   }

   const auto &cbWrap = [cb](bool result, const SecureBinaryData &pubKey)
   {
      if (cb) {
         cb(result ? pubKey : SecureBinaryData{});
      }
   };
   signContainer_->getRootPubkey(walletId(), cbWrap);
}
#endif   //0
