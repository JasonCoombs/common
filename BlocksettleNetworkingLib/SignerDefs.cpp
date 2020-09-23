/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SignerDefs.h"
#include "CoreWalletsManager.h"
#include "CoreHDWallet.h"
#include "Wallets/SyncHDWallet.h"

#include "common.pb.h"
#include "headless.pb.h"

using namespace Blocksettle::Communication;
using namespace BlockSettle::Common;
using namespace bs::sync;

NetworkType bs::sync::mapFrom(const headless::NetworkType &netType)
{
   switch (netType) {
   case headless::MainNetType:   return NetworkType::MainNet;
   case headless::TestNetType:   return NetworkType::TestNet;
   default:    return NetworkType::Invalid;
   }
}

bs::sync::WalletFormat bs::sync::mapFrom(const headless::WalletFormat &format)
{
   switch (format) {
   case headless::WalletFormatHD:         return bs::sync::WalletFormat::HD;
   case headless::WalletFormatPlain:      return bs::sync::WalletFormat::Plain;
   case headless::WalletFormatSettlement: return bs::sync::WalletFormat::Settlement;
   case headless::WalletFormatUnknown:
   default:    return bs::sync::WalletFormat::Unknown;
   }
}

WalletData WalletData::fromPbMessage(const headless::SyncWalletResponse &response)
{
   WalletData result;

   result.highestExtIndex = response.highest_ext_index();
   result.highestIntIndex = response.highest_int_index();

   for (int i = 0; i < response.addresses_size(); ++i) {
      const auto addrInfo = response.addresses(i);
      const auto addr = bs::Address::fromAddressString(addrInfo.address());
      if (addr.empty()) {
         continue;
      }
      result.addresses.push_back({ addrInfo.index(), std::move(addr)
         , addrInfo.comment() });
   }
   for (int i = 0; i < response.addrpool_size(); ++i) {
      const auto addrInfo = response.addrpool(i);
      const auto addr = bs::Address::fromAddressString(addrInfo.address());
      if (addr.empty()) {
         continue;
      }
      result.addrPool.push_back({ addrInfo.index(), std::move(addr), "" });
   }
   for (int i = 0; i < response.txcomments_size(); ++i) {
      const auto txInfo = response.txcomments(i);
      result.txComments.push_back({ BinaryData::fromString(txInfo.txhash()), txInfo.comment() });
   }

   return result;
}

WalletData WalletData::fromCommonMessage(const WalletsMessage_WalletData &msg)
{
   WalletData result;

   for (const auto &usedAddr : msg.used_addresses()) {
      try {
         const auto &addr = bs::Address::fromAddressString(usedAddr.address());
         result.addresses.push_back({ usedAddr.index(), std::move(addr)
            , usedAddr.comment() });
      }
      catch (const std::exception &) {}
   }
   return result;
}

WalletsMessage_WalletData WalletData::toCommonMessage() const
{
   WalletsMessage_WalletData result;

   for (const auto &addr : addresses) {
      auto msgAddr = result.add_used_addresses();
      msgAddr->set_address(addr.address.display());
      msgAddr->set_index(addr.index);
      msgAddr->set_comment(addr.comment);
   }
   return result;
}


std::vector<bs::sync::WalletInfo> bs::sync::WalletInfo::fromPbMessage(const headless::SyncWalletInfoResponse &response)
{
   std::vector<bs::sync::WalletInfo> result;
   for (int i = 0; i < response.wallets_size(); ++i) {
      const auto walletInfoPb = response.wallets(i);
      bs::sync::WalletInfo walletInfo;

      walletInfo.format = mapFrom(walletInfoPb.format());
      walletInfo.ids.push_back(walletInfoPb.id());
      walletInfo.name = walletInfoPb.name();
      walletInfo.description = walletInfoPb.description();
      walletInfo.netType = mapFrom(walletInfoPb.nettype());
      walletInfo.watchOnly = walletInfoPb.watching_only();

      for (int i = 0; i < walletInfoPb.encryptiontypes_size(); ++i) {
         const auto encType = walletInfoPb.encryptiontypes(i);
         walletInfo.encryptionTypes.push_back(bs::sync::mapFrom(encType));
      }
      for (int i = 0; i < walletInfoPb.encryptionkeys_size(); ++i) {
         const auto encKey = walletInfoPb.encryptionkeys(i);
         walletInfo.encryptionKeys.push_back(BinaryData::fromString(encKey));
      }
      walletInfo.encryptionRank = { walletInfoPb.keyrank().m(), walletInfoPb.keyrank().n() };

      result.push_back(walletInfo);
   }
   return result;
}

bs::sync::WalletInfo bs::sync::WalletInfo::fromLeaf(const std::shared_ptr<bs::sync::hd::Leaf> &leaf)
{
   bs::sync::WalletInfo result;
   result.format = bs::sync::WalletFormat::Plain;
   result.ids = leaf->internalIds();
   result.type = leaf->type();
   result.purpose = leaf->purpose();
   result.name = leaf->shortName();
   result.description = leaf->description();
   result.watchOnly = leaf->isWatchingOnly();
   result.encryptionTypes = leaf->encryptionTypes();
   result.encryptionKeys = leaf->encryptionKeys();
   result.encryptionRank = leaf->encryptionRank();
   return result;
}

bs::sync::WalletInfo bs::sync::WalletInfo::fromWallet(const std::shared_ptr<bs::sync::hd::Wallet> &wallet)
{
   bs::sync::WalletInfo result;
   result.format = bs::sync::WalletFormat::HD;
   result.ids.push_back(wallet->walletId());
   result.netType = wallet->networkType();
   result.name = wallet->name();
   result.description = wallet->description();
   result.encryptionTypes = wallet->encryptionTypes();
   result.encryptionKeys = wallet->encryptionKeys();
   result.encryptionRank = wallet->encryptionRank();
   return result;
}

void bs::sync::WalletInfo::toCommonMsg(BlockSettle::Common::WalletInfo &msg) const
{
   msg.set_format((int)format);
   for (const auto &id : ids) {
      msg.add_id(id);
   }
   msg.set_name(name);
   msg.set_description(description);
   msg.set_network_type((int)netType);
   msg.set_watch_only(watchOnly);
   for (const auto &encType : encryptionTypes) {
      msg.add_encryption_types((int)encType);
   }
   for (const auto &encKey : encryptionKeys) {
      msg.add_encryption_keys(encKey.toBinStr());
   }
   auto keyRank = msg.mutable_encryption_rank();
   keyRank->set_m(encryptionRank.m);
   keyRank->set_n(encryptionRank.n);

   msg.set_wallet_type((int)type);
   msg.set_purpose(purpose);
}

bs::sync::WalletInfo bs::sync::WalletInfo::fromCommonMsg(const BlockSettle::Common::WalletInfo &msg)
{
   bs::sync::WalletInfo result;
   result.format = static_cast<bs::sync::WalletFormat>(msg.format());
   for (const auto &id : msg.id()) {
      result.ids.push_back(id);
   }
   result.name = msg.name();
   result.description = msg.description();
   result.netType = static_cast<NetworkType>(msg.network_type());
   result.watchOnly = msg.watch_only();
   for (const auto &encType : msg.encryption_types()) {
      result.encryptionTypes.push_back(static_cast<bs::wallet::EncryptionType>(encType));
   }
   for (const auto &encKey : msg.encryption_keys()) {
      result.encryptionKeys.push_back(BinaryData::fromString(encKey));
   }
   result.encryptionRank.m = msg.encryption_rank().m();
   result.encryptionRank.n = msg.encryption_rank().n();
   result.type = static_cast<bs::core::wallet::Type>(msg.wallet_type());
   result.purpose = static_cast<bs::hd::Purpose>(msg.purpose());
   result.primary = msg.primary();
   return result;
}

bs::wallet::EncryptionType bs::sync::mapFrom(const headless::EncryptionType &encType)
{
   switch (encType) {
   case headless::EncryptionTypePassword:    return bs::wallet::EncryptionType::Password;
   case headless::EncryptionTypeAutheID:     return bs::wallet::EncryptionType::Auth;
   case headless::EncryptionTypeHw:          return bs::wallet::EncryptionType::Hardware;
   case headless::EncryptionTypeUnencrypted:
   default:    return bs::wallet::EncryptionType::Unencrypted;
   }
}

headless::SyncWalletInfoResponse bs::sync::exportHDWalletsInfoToPbMessage(const std::shared_ptr<bs::core::WalletsManager> &walletsMgr)
{
   headless::SyncWalletInfoResponse response;
   assert(walletsMgr);

   if (!walletsMgr) {
      return response;
   }

   for (size_t i = 0; i < walletsMgr->getHDWalletsCount(); ++i) {
      auto wallet = response.add_wallets();
      const auto hdWallet = walletsMgr->getHDWallet(i);
      wallet->set_format(headless::WalletFormatHD);
      wallet->set_id(hdWallet->walletId());
      wallet->set_name(hdWallet->name());
      wallet->set_description(hdWallet->description());
      wallet->set_nettype(mapFrom(hdWallet->networkType()));
      wallet->set_watching_only(hdWallet->isWatchingOnly());

      for (const auto &encType : hdWallet->encryptionTypes()) {
         wallet->add_encryptiontypes(bs::sync::mapFrom(encType));
      }
      for (const auto &encKey : hdWallet->encryptionKeys()) {
         wallet->add_encryptionkeys(encKey.toBinStr());
      }
      auto keyrank = wallet->mutable_keyrank();
      keyrank->set_m(hdWallet->encryptionRank().m);
      keyrank->set_n(hdWallet->encryptionRank().n);
   }
   return response;
}

headless::EncryptionType bs::sync::mapFrom(bs::wallet::EncryptionType encType)
{
   switch (encType) {
   case bs::wallet::EncryptionType::Password:         return headless::EncryptionTypePassword;
   case bs::wallet::EncryptionType::Auth:             return headless::EncryptionTypeAutheID;
   case bs::wallet::EncryptionType::Hardware:         return headless::EncryptionTypeHw;
   case bs::wallet::EncryptionType::Unencrypted:
   default:       return headless::EncryptionTypeUnencrypted;
   }
}

headless::NetworkType bs::sync::mapFrom(NetworkType netType)
{
   switch (netType) {
   case NetworkType::MainNet: return headless::MainNetType;
   case NetworkType::TestNet:
   default:    return headless::TestNetType;
   }
}

headless::SyncWalletResponse bs::sync::exportHDLeafToPbMessage(const std::shared_ptr<bs::core::hd::Leaf> &leaf)
{
   headless::SyncWalletResponse response;
   response.set_walletid(leaf->walletId());

   response.set_highest_ext_index(leaf->getExtAddressCount());
   response.set_highest_int_index(leaf->getIntAddressCount());

   for (const auto &addr : leaf->getUsedAddressList()) {
      const auto comment = leaf->getAddressComment(addr);
      const auto index = leaf->getAddressIndex(addr);
      auto addrData = response.add_addresses();
      addrData->set_address(addr.display());
      addrData->set_index(index);
      if (!comment.empty()) {
         addrData->set_comment(comment);
      }
   }
   const auto &pooledAddresses = leaf->getPooledAddressList();
   for (const auto &addr : pooledAddresses) {
      const auto index = leaf->getAddressIndex(addr);
      auto addrData = response.add_addrpool();
      addrData->set_address(addr.display());
      addrData->set_index(index);
   }
   for (const auto &txComment : leaf->getAllTxComments()) {
      auto txCommData = response.add_txcomments();
      txCommData->set_txhash(txComment.first.toBinStr());
      txCommData->set_comment(txComment.second);
   }
   return response;
}


BlockSettle::Common::HDWalletData bs::sync::HDWalletData::toCommonMessage() const
{
   BlockSettle::Common::HDWalletData result;
   result.set_wallet_id(id);
   result.set_is_primary(primary);
   for (const auto &group : groups) {
      auto msgGroup = result.add_groups();
      msgGroup->set_type(static_cast<int>(group.type));
      msgGroup->set_name(group.name);
      msgGroup->set_desc(group.description);
      msgGroup->set_ext_only(group.extOnly);
      if (!group.salt.empty()) {
         msgGroup->set_salt(group.salt.toBinStr());
      }
      for (const auto &leaf : group.leaves) {
         auto msgLeaf = msgGroup->add_leaves();
         for (const auto &id : leaf.ids) {
            msgLeaf->add_ids(id);
         }
         msgLeaf->set_path(leaf.path.toString());
         msgLeaf->set_name(leaf.name);
         msgLeaf->set_desc(leaf.description);
         msgLeaf->set_ext_only(leaf.extOnly);
         if (!leaf.extraData.empty()) {
            msgLeaf->set_extra_data(leaf.extraData.toBinStr());
         }
      }
   }
   return result;
}

bs::sync::HDWalletData bs::sync::HDWalletData::fromCommonMessage(
   const BlockSettle::Common::HDWalletData &msg)
{
   HDWalletData result;
   result.id = msg.wallet_id();
   result.primary = msg.is_primary();
   for (const auto &msgGroup : msg.groups()) {
      HDWalletData::Group group;
      group.type = static_cast<bs::hd::CoinType>(msgGroup.type());
      group.name = msgGroup.name();
      group.description = msgGroup.desc();
      group.extOnly = msgGroup.ext_only();
      group.salt = BinaryData::fromString(msgGroup.salt());

      for (const auto &msgLeaf : msgGroup.leaves()) {
         HDWalletData::Leaf leaf;
         for (const auto &id : msgLeaf.ids()) {
            leaf.ids.push_back(id);
         }
         leaf.path = bs::hd::Path::fromString(msgLeaf.path());
         leaf.name = msgLeaf.name();
         leaf.description = msgLeaf.desc();
         leaf.extOnly = msgLeaf.ext_only();
         leaf.extraData = BinaryData::fromString(msgLeaf.extra_data());
         group.leaves.emplace_back(std::move(leaf));
      }
      result.groups.emplace_back(std::move(group));
   }
   return result;
}
