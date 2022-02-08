/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "InprocSigner.h"
#include <spdlog/spdlog.h>
#include "Address.h"
#include "CoreWalletsManager.h"
#include "CoreHDWallet.h"
#include "Wallets/SyncHDWallet.h"

#include "headless.pb.h"

using namespace Blocksettle::Communication::headless;

InprocSigner::InprocSigner(const std::shared_ptr<bs::core::WalletsManager> &mgr
   , const std::shared_ptr<spdlog::logger> &logger, SignerCallbackTarget* sct
   , const std::string &walletsPath, ::NetworkType netType, const PwdLockCb& cb)
   : WalletSignerContainer(logger, sct, SignContainer::OpMode::LocalInproc)
   , walletsMgr_(mgr), walletsPath_(walletsPath), netType_(netType)
   , pwLockCb_(cb)
{ }

InprocSigner::InprocSigner(const std::shared_ptr<bs::core::hd::Wallet> &wallet
   , SignerCallbackTarget* sct, const std::shared_ptr<spdlog::logger> &logger
   , const PwdLockCb& cb)
   : WalletSignerContainer(logger, sct, SignContainer::OpMode::LocalInproc)
   , walletsPath_({}), netType_(wallet->networkType()), pwLockCb_(cb)
{
   walletsMgr_ = std::make_shared<bs::core::WalletsManager>(logger);
   walletsMgr_->addWallet(wallet);
}

void InprocSigner::Start()
{
   if (!walletsPath_.empty() && !walletsMgr_->walletsLoaded()) {
      const auto &cbLoadProgress = [this](size_t cur, size_t total) {
         logger_->debug("[InprocSigner::Start] loading wallets: {} of {}", cur, total);
      };
      walletsMgr_->loadWallets(netType_, walletsPath_, {}, cbLoadProgress);
   }
   inited_ = true;
   if (sct_) {
      sct_->onReady();
   }
}

// All signing code below doesn't include password request support for encrypted wallets - i.e.
// a password should be passed directly to signing methods

void InprocSigner::signTXRequest(const bs::core::wallet::TXSignRequest& txSignReq
   , const std::function<void(const BinaryData &signedTX, bs::error::ErrorCode
      , const std::string& errorReason)>& cb
   , TXSignMode mode, bool keepDuplicatedRecipients)
{
   if (!txSignReq.isValid()) {
      logger_->error("[{}] Invalid TXSignRequest", __func__);
      cb({}, bs::error::ErrorCode::InternalError, "invalid request");
      return;
   }
   std::vector<std::shared_ptr<bs::core::Wallet>> wallets;
   for (const auto& walletId : txSignReq.walletIds) {
      const auto wallet = walletsMgr_->getWalletById(walletId);
      if (!wallet) {
         logger_->error("[{}] failed to find wallet with id {}", __func__, walletId);
         cb({}, bs::error::ErrorCode::InternalError, "wallet not found");
         return;
      }
      wallets.push_back(wallet);
   }
   if (wallets.empty()) {
      logger_->error("[{}] empty wallets list", __func__);
      cb({}, bs::error::ErrorCode::InternalError, "empty wallets");
      return;
   }

   const auto reqId = seqId_++;
   try {
      BinaryData signedTx;
      PasswordLock pwLock = pwLockCb_ ? std::move(pwLockCb_(wallets.front()->walletId())) : nullptr;
      if (mode == TXSignMode::Full) {
         if (wallets.size() == 1) {
            signedTx = wallets.front()->signTXRequest(txSignReq);
         } else {
            bs::core::wallet::TXMultiSignRequest multiReq;
            multiReq.armorySigner_.merge(txSignReq.armorySigner_);

            bs::core::WalletMap wallets;
            for (unsigned i = 0; i < txSignReq.armorySigner_.getTxInCount(); i++) {
               auto utxo = txSignReq.armorySigner_.getSpender(i)->getUtxo();
               const auto addr = bs::Address::fromUTXO(utxo);
               const auto wallet = walletsMgr_->getWalletByAddress(addr);
               if (!wallet) {
                  logger_->error("[{}] failed to find wallet for input address {}"
                     , __func__, addr.display());
                  cb({}, bs::error::ErrorCode::InternalError, "failed to find wallet for input address");
                  return;
               }
               multiReq.addWalletId(wallet->walletId());
               wallets[wallet->walletId()] = wallet;
            }
            multiReq.RBF = txSignReq.RBF;

            signedTx = bs::core::SignMultiInputTX(multiReq, wallets);
         }
      } else {
         if (wallets.size() != 1) {
            logger_->error("[{}] can't sign partial request for more than 1 wallet", __func__);
            cb({}, bs::error::ErrorCode::InternalError, "can't sign partial request for more than 1 wallet");
            return;
         }
         signedTx = BinaryData::fromString(wallets.front()->signPartialTXRequest(txSignReq).SerializeAsString());
      }
      cb(signedTx, bs::error::ErrorCode::NoError, {});
   } catch (const std::exception& e) {
      cb({}, bs::error::ErrorCode::InternalError, e.what());
   }
}

bs::signer::RequestId InprocSigner::resolvePublicSpenders(const bs::core::wallet::TXSignRequest &txReq
   , const SignerStateCb &cb)
{
   std::set<std::shared_ptr<bs::core::Wallet>> wallets;
   for (unsigned i=0; i<txReq.armorySigner_.getTxInCount(); i++) {
      const auto& utxo = txReq.armorySigner_.getSpender(i)->getUtxo();
      const auto &addr = bs::Address::fromUTXO(utxo);
      const auto &wallet = walletsMgr_->getWalletByAddress(addr);
      if (wallet) {
         wallets.insert(wallet);
      }
   }
   if (wallets.empty()) {
      logger_->error("[{}] failed to find any associated wallets", __func__);
      return 0;
   }

   Armory::Signer::Signer signer(txReq.armorySigner_);
   const auto reqId = seqId_++;
   for (const auto &wallet : wallets) {
      signer.resetFeed();
      signer.setFeed(wallet->getPublicResolver());
      signer.resolvePublicData();
   }
   const auto &resolvedState = signer.serializeState();
   cb(resolvedState.IsInitialized() ? bs::error::ErrorCode::NoError : bs::error::ErrorCode::InternalError
      , resolvedState);
   return reqId;
}

bool InprocSigner::createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &path
   , const std::vector<bs::wallet::PasswordData> &pwdData
   , bs::sync::PasswordDialogData
   , const CreateHDLeafCb &cb)
{
   const auto hdWallet = walletsMgr_->getHDWalletById(rootWalletId);
   if (!hdWallet) {
      logger_->error("[InprocSigner::createHDLeaf] failed to get HD wallet by id {}", rootWalletId);
      if (cb) {
         cb(bs::error::ErrorCode::WalletNotFound, {});
      }
      return false;
   }
   if (path.length() != 3) {
      logger_->error("[InprocSigner::createHDLeaf] too short path: {}", path.toString());
      if (cb) {
         cb(bs::error::ErrorCode::WalletNotFound, {});
      }
      return false;
   }
   const auto groupType = static_cast<bs::hd::CoinType>(path.get(-2));
   const auto group = hdWallet->createGroup(groupType);
   if (!group) {
      logger_->error("[InprocSigner::createHDLeaf] failed to create/get group for {}", path.get(-2));
      if (cb) {
         cb(bs::error::ErrorCode::WalletNotFound, {});
      }
      return false;
   }

   if (!walletsPath_.empty()) {
      walletsMgr_->backupWallet(hdWallet, walletsPath_);
   }

   std::shared_ptr<bs::core::hd::Leaf> leaf;

   try {
      const auto& password = pwdData[0].password;

      auto leaf = group->createLeaf(path);
      if (leaf != nullptr) {
         if (cb) {
            cb(bs::error::ErrorCode::NoError, leaf->walletId());
         }
         return true;
      }
   }
   catch (const std::exception &e) {
      logger_->error("[InprocSigner::createHDLeaf] failed to decrypt root node {}: {}"
         , rootWalletId, e.what());
   }

   if (cb) {
      cb(bs::error::ErrorCode::InvalidPassword, {});
   }
   return false;
}

#if 0 // settlement is being removed
void InprocSigner::createSettlementWallet(const bs::Address &authAddr
   , const std::function<void(const SecureBinaryData &)> &cb)
{
   const auto priWallet = walletsMgr_->getPrimaryWallet();
   if (!priWallet) {
      if (cb) {
         cb({});
      }
      return;
   }

   const auto leaf = priWallet->createSettlementLeaf(authAddr);
   if (!leaf) {
      if (cb) {
         cb({});
      }
      return;
   }
   const auto &cbWrap = [cb](bool, const SecureBinaryData &pubKey) {
      if (cb) {
         cb(pubKey);
      }
   };

   getRootPubkey(priWallet->walletId(), cbWrap);
}
#endif   //0

bs::signer::RequestId InprocSigner::DeleteHDRoot(const std::string &walletId)
{
   const auto wallet = walletsMgr_->getHDWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] failed to find wallet with id {}", __func__, walletId);
      return 0;
   }
   if (walletsMgr_->deleteWalletFile(wallet)) {
      return seqId_++;
   }
   return 0;
}

bs::signer::RequestId InprocSigner::DeleteHDLeaf(const std::string &walletId)
{
   const auto wallet = walletsMgr_->getWalletById(walletId);
   if (!wallet) {
      logger_->error("[{}] failed to find wallet with id {}", __func__, walletId);
      return 0;
   }
   if (walletsMgr_->deleteWalletFile(wallet)) {
      return seqId_++;
   }
   return 0;
}

bs::signer::RequestId InprocSigner::GetInfo(const std::string &walletId)
{
   auto hdWallet = walletsMgr_->getHDWalletById(walletId);
   if (!hdWallet) {
      hdWallet = walletsMgr_->getHDRootForLeaf(walletId);
      if (!hdWallet) {
         logger_->error("[{}] failed to get wallet by id {}", __func__, walletId);
         return 0;
      }
   }
   const auto reqId = seqId_++;
   if (sct_) {
      GetHDWalletInfoResponse wi;
      wi.set_rootwalletid(hdWallet->walletId());
      wi.set_rankm(hdWallet->encryptionRank().m);
      wi.set_rankn(hdWallet->encryptionRank().n);
      for (const auto& encKey : hdWallet->encryptionKeys()) {
         wi.add_enckeys(encKey.toBinStr());
      }
      for (const auto& encType : hdWallet->encryptionTypes()) {
         wi.add_enctypes((int)encType);
      }
      sct_->walletInfo(reqId, wi);
   }
   return reqId;
}

void InprocSigner::syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &cb)
{
   std::vector<bs::sync::WalletInfo> result;
   for (size_t i = 0; i < walletsMgr_->getHDWalletsCount(); ++i)
   {
      const auto hdWallet = walletsMgr_->getHDWallet(i);
      bs::sync::WalletInfo walletInfo;

      walletInfo.format = bs::sync::WalletFormat::HD;
      walletInfo.ids.push_back(hdWallet->walletId());
      walletInfo.name = hdWallet->name();
      walletInfo.description = hdWallet->description();
      walletInfo.netType = hdWallet->networkType();
      walletInfo.watchOnly = hdWallet->isWatchingOnly();

      walletInfo.encryptionTypes = hdWallet->encryptionTypes();
      walletInfo.encryptionKeys = hdWallet->encryptionKeys();
      walletInfo.encryptionRank = hdWallet->encryptionRank();
      walletInfo.netType = hdWallet->networkType();

      result.push_back(walletInfo);
   }

   cb(result);
}

void InprocSigner::syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &cb)
{
   bs::sync::HDWalletData result;
   const auto hdWallet = walletsMgr_->getHDWalletById(id);
   if (hdWallet) {
      for (const auto &group : hdWallet->getGroups()) {
         bs::sync::HDWalletData::Group groupData;
         groupData.type = static_cast<bs::hd::CoinType>(group->index() | bs::hd::hardFlag);
         groupData.extOnly = group->isExtOnly();

         if (groupData.type == bs::hd::CoinType::BlockSettle_Auth) {
            auto authGroupPtr =
               std::dynamic_pointer_cast<bs::core::hd::AuthGroup>(group);
            if (authGroupPtr == nullptr)
               throw std::runtime_error("unexpected group type");

            groupData.salt = authGroupPtr->getSalt();
         }

         for (const auto &leaf : group->getAllLeaves()) {
            BinaryData extraData;
            if (groupData.type == bs::hd::CoinType::BlockSettle_Settlement) {
               const auto settlLeaf = std::dynamic_pointer_cast<bs::core::hd::SettlementLeaf>(leaf);
               if (settlLeaf == nullptr) {
                  throw std::runtime_error("unexpected leaf type");
               }
               const auto rootAsset = settlLeaf->getRootAsset();
               const auto rootSingle = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_Single>(rootAsset);
               if (rootSingle == nullptr) {
                  throw std::runtime_error("invalid root asset");
               }
               extraData = BtcUtils::getHash160(rootSingle->getPubKey()->getCompressedKey());
            }
            groupData.leaves.push_back({ { leaf->walletId() }, leaf->path()
               , leaf->shortName(), std::string{}, leaf->hasExtOnlyAddresses(), std::move(extraData) });
         }

         result.groups.push_back(groupData);
      }
   }
   else {
      logger_->error("[{}] failed to find HD wallet with id {}", __func__, id);
   }

   cb(result);
}

void InprocSigner::syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &cb)
{
   bs::sync::WalletData result;
   const auto wallet = walletsMgr_->getWalletById(id);
   if (!wallet) {
      cb(result);
      return;
   }
   const auto rootWallet = walletsMgr_->getHDRootForLeaf(wallet->walletId());
   if (!rootWallet) {
      cb(result);
      return;
   }

   result.highestExtIndex = wallet->getExtAddressCount();
   result.highestIntIndex = wallet->getIntAddressCount();

   size_t addrCnt = 0;
   for (const auto &addr : wallet->getUsedAddressList()) {
      const auto index = wallet->getAddressIndex(addr);
      const auto comment = wallet->getAddressComment(addr);
      result.addresses.push_back({ index, addr, comment });
   }

   for (const auto &addr : wallet->getPooledAddressList()) {
      const auto index = wallet->getAddressIndex(addr);
      result.addrPool.push_back({ index, addr, {} });
   }

   for (const auto &txComment : wallet->getAllTxComments()) {
      result.txComments.push_back({ txComment.first, txComment.second });
   }
   cb(result);
}

void InprocSigner::syncAddressComment(const std::string &walletId, const bs::Address &addr, const std::string &comment)
{
   const auto wallet = walletsMgr_->getWalletById(walletId);
   if (wallet)
      wallet->setAddressComment(addr, comment);
}

void InprocSigner::syncTxComment(const std::string &walletId, const BinaryData &txHash, const std::string &comment)
{
   const auto wallet = walletsMgr_->getWalletById(walletId);
   if (wallet)
      wallet->setTransactionComment(txHash, comment);
}

void InprocSigner::syncNewAddresses(const std::string &walletId
   , const std::vector<std::string> &inData
   , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &cb)
{
   std::vector<std::pair<bs::Address, std::string>> result;
   const auto wallet = walletsMgr_->getWalletById(walletId);
   if (wallet == nullptr) {
      if (cb) {
         cb(result);
      }
      return;
   }

   result.reserve(inData.size());
   for (const auto &in : inData) {
      std::string index;
      try {
         const auto addr = bs::Address::fromAddressString(in);
         if (addr.isValid()) {
            index = wallet->getAddressIndex(addr);
         }
      }
      catch (const std::exception&) { }

      if (index.empty()) {
         index = in;
      }

      result.push_back({ wallet->synchronizeUsedAddressChain(in).first, in });
   }

   if (cb) {
      cb(result);
   }
}

void InprocSigner::extendAddressChain(
   const std::string &walletId, unsigned count, bool extInt,
   const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &cb)
{  /***
   Extend the wallet's account external (extInt == true) or internal
   (extInt == false) chain, return the newly created addresses.

   These are not instantiated addresses, but pooled ones. They represent
   possible address type variations of the newly created assets, a set
   necessary to properly register the wallet with ArmoryDB.
   ***/

   std::vector<std::pair<bs::Address, std::string>> result;
   const auto wallet = walletsMgr_->getWalletById(walletId);
   if (wallet == nullptr) {
      cb(result);
      return;
   }

   auto&& newAddrVec = wallet->extendAddressChain(count, extInt);
   for (auto& addr : newAddrVec) {
      auto&& index = wallet->getAddressIndex(addr);
      auto addrPair = std::make_pair(addr, index);
      result.emplace_back(addrPair);
   }

   cb(result);
}

void InprocSigner::syncAddressBatch(
   const std::string &walletId, const std::set<BinaryData>& addrSet,
   std::function<void(bs::sync::SyncState)> cb)
{
   //grab wallet
   const auto wallet = walletsMgr_->getWalletById(walletId);
   if (wallet == nullptr) {
      cb(bs::sync::SyncState::NothingToDo);
      return;
   }

   //resolve the path and address type for addrSet
   std::map<BinaryData, bs::hd::Path> parsedMap;
   try {
      parsedMap = wallet->indexPath(addrSet);
   }
   catch (const Armory::Accounts::AccountException &) {
      //failure to find even one of the addresses means the wallet chain needs
      //extended further
      cb(bs::sync::SyncState::Failure);
   }

   //order addresses by path
   typedef std::set<bs::hd::Path> PathSet;
   std::map<bs::hd::Path::Elem, PathSet> mapByPath;

   for (auto& parsedPair : parsedMap) {
      auto elem = parsedPair.second.get(-2);
      auto& mapping = mapByPath[elem];
      mapping.insert(parsedPair.second);
   }

   //request each chain for the relevant address types
   bool update = false;
   for (const auto &mapping : mapByPath) {
      for (const auto &path : mapping.second) {
         auto resultPair = wallet->synchronizeUsedAddressChain(path.toString());
         update |= resultPair.second;
      }
   }

   if (update) {
      cb(bs::sync::SyncState::Success);
   }
   else {
      cb(bs::sync::SyncState::NothingToDo);
   }
}

#if 0 // settlement is being removed
void InprocSigner::setSettlementID(const std::string& wltId
   , const SecureBinaryData &settlId, const std::function<void(bool)> &cb)
{  /***
   For remote methods, the caller should wait on return before
   proceeding further with settlement flow.
   ***/

   auto leafPtr = walletsMgr_->getWalletById(wltId);
   auto settlLeafPtr =
      std::dynamic_pointer_cast<bs::core::hd::SettlementLeaf>(leafPtr);
   if (settlLeafPtr == nullptr) {
      if (cb) {
         cb(false);
      }
      return;
   }
   settlLeafPtr->addSettlementID(settlId);

   /*
   Grab the id so that the address is in the asset map. These
   aren't useful to the sync wallet as they never see coins and
   aren't registered.
   */
   settlLeafPtr->getNewExtAddress();
   if (cb) {
      cb(true);
   }
}

void InprocSigner::getSettlementPayinAddress(const std::string& walletID
   , const bs::core::wallet::SettlementData &sd
   , const std::function<void(bool, bs::Address)> &cb)
{
   auto wltPtr = walletsMgr_->getHDWalletById(walletID);
   if (wltPtr == nullptr) {
      if (cb) {
         cb(false, {});
      }
      return;
   }

   if (cb) {
      cb(true, wltPtr->getSettlementPayinAddress(sd));
   }
}
#endif   //0

void InprocSigner::getRootPubkey(const std::string& walletID
   , const std::function<void(bool, const SecureBinaryData &)> &cb)
{
   auto leafPtr = walletsMgr_->getWalletById(walletID);
   auto rootPtr = leafPtr->getRootAsset();
   auto rootSingle = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_Single>(rootPtr);
   if (rootSingle == nullptr) {
      if (cb) {
         cb(false, {});
      }
   }

   if (cb) {
      cb(true, rootSingle->getPubKey()->getCompressedKey());
   }
}

std::shared_ptr<bs::core::hd::Leaf> InprocSigner::getAuthLeaf() const
{
   const auto &priWallet = walletsMgr_->getPrimaryWallet();
   if (priWallet) {
      const auto &authGroup = priWallet->getGroup(bs::hd::BlockSettle_Auth);
      if (authGroup) {
         const bs::hd::Path authPath({ bs::hd::Purpose::Native, bs::hd::BlockSettle_Auth, 0 });
         return authGroup->getLeafByPath(authPath);
      }
   }
   return nullptr;
}

#if 0 // settlement is being removed
void InprocSigner::setSettlAuthAddr(const std::string &walletId, const BinaryData &settlId
   , const bs::Address &addr)
{
   const auto &authLeaf = getAuthLeaf();
   if (authLeaf) {
      authLeaf->setSettlementMeta(settlId, addr);
   }
}

void InprocSigner::getSettlAuthAddr(const std::string &walletId, const BinaryData &settlId
   , const std::function<void(const bs::Address &)> &cb)
{
   const auto &authLeaf = getAuthLeaf();
   if (authLeaf) {
      cb(authLeaf->getSettlAuthAddr(settlId));
   }
   else {
      cb({});
   }
}

void InprocSigner::setSettlCP(const std::string &walletId, const BinaryData &payinHash
   , const BinaryData &settlId, const BinaryData &cpPubKey)
{
   const auto &authLeaf = getAuthLeaf();
   if (authLeaf) {
      authLeaf->setSettlCPMeta(payinHash, settlId, cpPubKey);
   }
}

void InprocSigner::getSettlCP(const std::string &walletId, const BinaryData &payinHash
   , const std::function<void(const BinaryData &, const BinaryData &)> &cb)
{
   const auto &authLeaf = getAuthLeaf();
   if (authLeaf) {
      const auto &keys = authLeaf->getSettlCP(payinHash);
      cb(keys.first, keys.second);
   }
   else {
      cb({}, {});
   }
}
#endif   //0
