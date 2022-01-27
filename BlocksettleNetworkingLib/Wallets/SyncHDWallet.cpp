/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SyncHDWallet.h"

#include "CheckRecipSigner.h"
#include "SyncWallet.h"
#include "WalletSignerContainer.h"

#include <QtConcurrent/QtConcurrentRun>

#define LOG(logger, method, ...) \
if ((logger)) { \
   logger->method(__VA_ARGS__); \
}

using namespace bs::sync;

hd::Wallet::Wallet(const bs::sync::WalletInfo &info, WalletSignerContainer *container
   , const std::shared_ptr<spdlog::logger> &logger)
   : walletId_(*info.ids.cbegin()), name_(info.name), desc_(info.description)
   , netType_(info.netType)
   , signContainer_(container), logger_(logger)
   , isOffline_(info.watchOnly)
{
   netType_ = getNetworkType();  // netType_ = info.netType ???
   bool isHw = false;
   for (auto type : info.encryptionTypes) {
      if (bs::wallet::EncryptionType::Hardware == type) {
         isHw = true;
         break;
      }
   }

   if (info.watchOnly && !isHw) {
      encryptionTypes_ = { bs::wallet::EncryptionType::Unencrypted };
   }
   else {
      encryptionTypes_ = info.encryptionTypes;
      encryptionKeys_ = info.encryptionKeys;
      encryptionRank_ = info.encryptionRank;
   }
}

hd::Wallet::Wallet(const bs::sync::WatchingOnlyWallet &info, WalletSignerContainer *container
   , const std::shared_ptr<spdlog::logger> &logger)
   : walletId_(info.id), name_(info.name), desc_(info.description)
   , signContainer_(container), logger_(logger), isOffline_(true)
{
   netType_ = getNetworkType();
   encryptionTypes_.push_back(bs::wallet::EncryptionType::Unencrypted);
}

hd::Wallet::~Wallet()
{
   for (auto &group : groups_) {
      group.second->resetWCT();
   }
}

void hd::Wallet::synchronize(const std::function<void()> &cbDone)
{
   if (!signContainer_) {
      return;
   }
   const auto &cbProcess = [this, cbDone](HDWalletData data)
   {
      for (const auto &grpData : data.groups) {
         auto group = getGroup(grpData.type);
         if (!group) {
            group = createGroup(grpData.type, grpData.extOnly);
#if 0
            if (grpData.type == bs::hd::CoinType::BlockSettle_Auth &&
               grpData.salt.getSize() == 32) {
               auto authGroupPtr =
                  std::dynamic_pointer_cast<hd::AuthGroup>(group);
               if (authGroupPtr == nullptr)
                  throw std::runtime_error("unexpected sync group type");

               authGroupPtr->setUserId(grpData.salt);
            }
#endif
         }
         if (!group) {
            LOG(logger_, error, "[hd::Wallet::synchronize] failed to create group {}", (uint32_t)grpData.type);
            continue;
         }

         for (const auto &leafData : grpData.leaves) {
            auto leaf = group->getLeaf(leafData.path);
            if (!leaf) {
               if (leafData.ids.empty()) {
                  LOG(logger_, error, "[hd::Wallet::synchronize] no id for leaf {}"
                     , leafData.path.toString());
                  continue;
               }
               leaf = group->createLeaf(leafData.path, *leafData.ids.cbegin());
            }
            if (!leaf) {
               LOG(logger_, error, "[hd::Wallet::synchronize] failed to create leaf {}/{} with id {}"
                  , (uint32_t)grpData.type, leafData.path.toString(), *leafData.ids.cbegin());
               continue;
            }
#if 0
            if (grpData.type == bs::hd::CoinType::BlockSettle_Settlement) {
               if (leafData.extraData.empty()) {
                  throw std::runtime_error("no extra data for settlement leaf " + *leafData.ids.cbegin());
               }
               const auto settlGroup = std::dynamic_pointer_cast<hd::SettlementGroup>(group);
               if (!settlGroup) {
                  throw std::runtime_error("invalid settlement group type");
               }
               settlGroup->addMap(leafData.extraData, leafData.path);
            }
#endif
         }
      }

      const auto leaves = getLeaves();
      auto leafIds = std::make_shared<std::set<std::string>>();
      for (const auto &leaf : leaves)
         leafIds->insert(leaf->walletId());

      for (const auto &leaf : leaves) {
         const auto &cbLeafDone = [leafIds, cbDone, id=leaf->walletId()]
         {
            leafIds->erase(id);
            if (leafIds->empty() && cbDone)
               cbDone();
         };
         leaf->synchronize(cbLeafDone);
      }
   };

   signContainer_->syncHDWallet(walletId(), cbProcess);
}

std::string hd::Wallet::walletId() const
{
   return walletId_;
}

std::vector<std::shared_ptr<hd::Group>> hd::Wallet::getGroups() const
{
   std::vector<std::shared_ptr<hd::Group>> result;
   result.reserve(groups_.size());
   {
      for (const auto &group : groups_) {
         result.emplace_back(group.second);
      }
   }
   return result;
}

size_t hd::Wallet::getNumLeaves() const
{
   size_t result = 0;
   {
      for (const auto &group : groups_) {
         result += group.second->getNumLeaves();
      }
   }
   return result;
}

std::vector<std::shared_ptr<bs::sync::Wallet>> hd::Wallet::getLeaves() const
{
   const std::lock_guard<std::mutex> lock(leavesLock_);

   const auto nbLeaves = getNumLeaves();
   if (leaves_.size() != nbLeaves) {
      leaves_.clear();
      for (const auto &group : groups_) {
         const auto &groupLeaves = group.second->getAllLeaves();
         for (const auto &leaf : groupLeaves) {
            leaves_[leaf->walletId()] = leaf;
         }
      }
   }

   std::vector<std::shared_ptr<bs::sync::Wallet>> result;
   result.reserve(leaves_.size());
   for (const auto &leaf : leaves_) {
      result.emplace_back(leaf.second);
   }
   return result;
}

std::shared_ptr<bs::sync::Wallet> hd::Wallet::getLeaf(const std::string &id) const
{
   const std::lock_guard<std::mutex> lock(leavesLock_);

   const auto &itLeaf = leaves_.find(id);
   if (itLeaf == leaves_.end()) {
      return nullptr;
   }
   return itLeaf->second;
}

BTCNumericTypes::balance_type hd::Wallet::getTotalBalance() const
{
   BTCNumericTypes::balance_type result = 0;
   const auto grp = getGroup(getXBTGroupType());
   if (grp) {
      for (const auto &leaf : grp->getAllLeaves()) {
         result += leaf->getTotalBalance();
      }
   }
   return result;
}

std::shared_ptr<hd::Group> hd::Wallet::createGroup(bs::hd::CoinType ct, bool isExtOnly)
{
   ct = static_cast<bs::hd::CoinType>(ct | bs::hd::hardFlag);
   std::shared_ptr<hd::Group> result;
   result = getGroup(ct);
   if (result) {
      return result;
   }

   switch (ct) {
#if 0
   case bs::hd::CoinType::BlockSettle_Auth:
      result = std::make_shared<hd::AuthGroup>(name_, desc_, signContainer_
         , this, logger_, isExtOnly);
      break;

   case bs::hd::CoinType::BlockSettle_CC:
      result = std::make_shared<hd::CCGroup>(name_, desc_, signContainer_
         , this, logger_, isExtOnly);
      break;

   case bs::hd::CoinType::BlockSettle_Settlement:
      result = std::make_shared<hd::SettlementGroup>(name_, desc_
         , signContainer_, this, logger_);
      break;
#endif
   default:
      result = std::make_shared<hd::Group>(ct, name_, hd::Group::nameForType(ct)
         , desc_, signContainer_, this, logger_, isExtOnly);
      break;
   }
   addGroup(result);
   return result;
}

void hd::Wallet::addGroup(const std::shared_ptr<hd::Group> &group)
{
   if (!userId_.empty()) {
      group->setUserId(userId_);
   }

   groups_[group->index()] = group;
}

std::shared_ptr<hd::Group> hd::Wallet::getGroup(bs::hd::CoinType ct) const
{
   const auto itGroup = groups_.find(static_cast<bs::hd::Path::Elem>(ct));
   if (itGroup == groups_.end()) {
      return nullptr;
   }
   return itGroup->second;
}

void hd::Wallet::walletCreated(const std::string &walletId)
{
   for (const auto &leaf : getLeaves()) {
      if ((leaf->walletId() == walletId) && armory_) {
         leaf->setArmory(armory_);
      }
   }

   if (wct_) {
      wct_->walletCreated(walletId);
   }
}

void hd::Wallet::walletDestroyed(const std::string &walletId)
{
   getLeaves();
   if (wct_) {
      wct_->walletDestroyed(walletId);
   }
}

void hd::Wallet::setUserId(const BinaryData &userId)
{
   userId_ = userId;
   std::vector<std::shared_ptr<hd::Group>> groups;
   groups.reserve(groups_.size());
   {
      for (const auto &group : groups_) {
         groups.push_back(group.second);
      }
   }
   for (const auto &group : groups) {
      group->setUserId(userId);
   }
}

void hd::Wallet::setArmory(const std::shared_ptr<ArmoryConnection> &armory)
{
   armory_ = armory;

   for (const auto &leaf : getLeaves()) {
      leaf->setArmory(armory);
   }
}

void hd::Wallet::scan(const std::function<void(bs::sync::SyncState)> &cb)
{
   auto stateMap = std::make_shared<std::map<std::string, bs::sync::SyncState>>();
   const auto &leaves = getLeaves();
   const auto nbLeaves = leaves.size();
   for (const auto &leaf : leaves) {
      const auto &cbScanLeaf = [this, leaf, nbLeaves, stateMap, cb](bs::sync::SyncState state) {
         (*stateMap)[leaf->walletId()] = state;
         if (stateMap->size() == nbLeaves) {
            bs::sync::SyncState hdState = bs::sync::SyncState::Failure;
            for (const auto &st : *stateMap) {
               if (st.second < hdState) {
                  hdState = st.second;
               }
            }
            leaf->synchronize([this, leaf, cb, hdState] {
               if (wct_) {
                  wct_->addressAdded(leaf->walletId());
               }
               if (cb) {
                  cb(hdState);
               }
            });
         }
      };
      logger_->debug("[{}] scanning leaf {}...", __func__, leaf->walletId());
      leaf->scan(cbScanLeaf);
   }
}

bs::hd::CoinType hd::Wallet::getXBTGroupType()
{
   return ((getNetworkType() == NetworkType::MainNet) ? bs::hd::CoinType::Bitcoin_main : bs::hd::CoinType::Bitcoin_test);
}

void hd::Wallet::startRescan()
{
   const auto &cbScanned = [this](bs::sync::SyncState state) {
      if (wct_) {
         wct_->scanComplete(walletId());
      }
   };
   scan(cbScanned);
}

bool hd::Wallet::deleteRemotely()
{
   if (!signContainer_) {
      return false;
   }
   return (signContainer_->DeleteHDRoot(walletId_) > 0);
}

bool hd::Wallet::isPrimary() const
{
   if (isOffline()) {
      return false;
   }

   return getGroup(bs::hd::CoinType::BlockSettle_Settlement) != nullptr;
}

bool hd::Wallet::tradingEnabled() const
{
   return getGroup(bs::hd::CoinType::BlockSettle_Auth) != nullptr;
}

std::vector<bs::wallet::EncryptionType> hd::Wallet::encryptionTypes() const
{
   return encryptionTypes_;
}

std::vector<BinaryData> hd::Wallet::encryptionKeys() const
{
   return encryptionKeys_;
}

void hd::Wallet::merge(const Wallet& rhs)
{
   const std::lock_guard<std::mutex> lock(leavesLock_);

   //rudimentary implementation, flesh it out on the go
   for (const auto &leafPair : rhs.leaves_) {
      auto iter = leaves_.find(leafPair.first);
      if (iter == leaves_.end()) {
         leaves_.insert(leafPair);
         continue;
      }

      auto& leafPtr = iter->second;
      leafPtr->merge(leafPair.second);
   }
}

void hd::Wallet::setWCT(WalletCallbackTarget *wct)
{
   wct_ = wct;
   for (const auto &leaf : getLeaves()) {
      leaf->setWCT(wct);
   }
}

bool bs::sync::hd::Wallet::isHardwareWallet() const
{
   auto hwEncKey = getHwEncKey();
   if (!hwEncKey) {
      return false;
   }

   return hwEncKey->deviceType()
      != bs::wallet::HardwareEncKey::WalletType::Offline;
}

bool bs::sync::hd::Wallet::isHardwareOfflineWallet() const
{
   auto hwEncKey = getHwEncKey();
   if (!hwEncKey) {
      return false;
   }

   return hwEncKey->deviceType()
      == bs::wallet::HardwareEncKey::WalletType::Offline;
}

bool bs::sync::hd::Wallet::canMixLeaves() const
{
   return encryptionTypes_.empty()
      || encryptionTypes_[0] != bs::wallet::EncryptionType::Hardware;
}

std::unique_ptr<bs::wallet::HardwareEncKey> bs::sync::hd::Wallet::getHwEncKey() const
{
   if (encryptionTypes_.empty() || encryptionKeys_.empty() ||
      encryptionTypes_[0] != bs::wallet::EncryptionType::Hardware) {
      return nullptr;
   }

   return std::make_unique<bs::wallet::HardwareEncKey>(encryptionKeys_[0]);
}

bool hd::Wallet::isFullWallet() const
{
   return !isOffline() && !isHardwareWallet()
      && !isHardwareOfflineWallet() && !isPrimary();
}
