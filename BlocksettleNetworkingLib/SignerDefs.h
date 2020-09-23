/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __SIGNER_DEFS_H__
#define __SIGNER_DEFS_H__

#include "Address.h"
#include "BtcDefinitions.h"
#include "CoreWallet.h"
#include "HDPath.h"
#include "WalletEncryption.h"
#include "Wallets/SyncWallet.h"


namespace bs {
   namespace core {
      class WalletsManager;
      namespace hd {
         class Leaf;
      }
   }
   namespace sync {
      namespace hd {
         class Leaf;
         class Wallet;
      }
   }
}
namespace Blocksettle {
   namespace Communication {
      namespace headless {
         enum EncryptionType : int;
         enum NetworkType : int;
         class SyncWalletInfoResponse;
         class SyncWalletResponse;
         enum WalletFormat : int;
      }
   }
}
namespace BlockSettle {
   namespace Common {
      class HDWalletData;
      class WalletInfo;
      class WalletsMessage_WalletData;
   }
}

namespace bs {
namespace signer {

   using RequestId = unsigned int;

   struct Limits {
      uint64_t    autoSignSpendXBT = UINT64_MAX;
      uint64_t    manualSpendXBT = UINT64_MAX;
      int         autoSignTimeS = 0;
      int         manualPassKeepInMemS = 0;

      Limits() {}
      Limits(uint64_t asXbt, uint64_t manXbt, int asTime, int manPwTime)
         : autoSignSpendXBT(asXbt), manualSpendXBT(manXbt), autoSignTimeS(asTime)
         , manualPassKeepInMemS(manPwTime) {}
   };

   enum class RunMode {
      fullgui,
      litegui,
      headless,
      cli
   };

   // Keep in sync with Blocksettle.Communication.signer.BindStatus
   enum class BindStatus
   {
      Inactive = 0,
      Succeed = 1,
      Failed = 2,
   };

   enum class AutoSignCategory
   {
      NotDefined = 0,
      RegularTx = 1,
      SettlementDealer = 2,
      SettlementRequestor = 3,
      SettlementOTC = 4,
      CreateLeaf = 5,
   };

} // signer

namespace sync {

   enum class WalletFormat {
      Unknown = 0,
      HD,
      Plain,
      Settlement
   };

   struct WalletInfo
   {
      static std::vector<WalletInfo> fromPbMessage(const Blocksettle::Communication::headless::SyncWalletInfoResponse &);
      static WalletInfo fromLeaf(const std::shared_ptr<bs::sync::hd::Leaf> &);
      static WalletInfo fromWallet(const std::shared_ptr<bs::sync::hd::Wallet> &);
      void toCommonMsg(BlockSettle::Common::WalletInfo &) const;
      static WalletInfo fromCommonMsg(const BlockSettle::Common::WalletInfo &);

      WalletFormat   format;
      std::vector<std::string>   ids;
      std::string name;
      std::string description;
      NetworkType netType;
      bool        watchOnly;

      std::vector<bs::wallet::EncryptionType>   encryptionTypes;
      std::vector<BinaryData> encryptionKeys;
      bs::wallet::KeyRank     encryptionRank{ 0,0 };

      bs::core::wallet::Type     type;
      bs::hd::Purpose            purpose;
      bool        primary{ false };

      bool operator<(const WalletInfo &other) const
      {
         return (ids < other.ids);
      }
      bool operator==(const WalletInfo &other) const
      {
         return (ids == other.ids);
      }
      bool operator!=(const WalletInfo &other) const
      {
         return !operator==(other);
      }
   };

   struct HDWalletData
   {
      struct Leaf {
         std::vector<std::string>   ids;
         bs::hd::Path   path;
         std::string    name;
         std::string    description;
         bool extOnly;
         BinaryData  extraData;
      };
      struct Group {
         bs::hd::CoinType  type;
         std::string       name;
         std::string       description;
         std::vector<Leaf> leaves;
         bool extOnly;
         BinaryData salt;
      };
      std::vector<Group>   groups;
      std::string          id;
      bool                 primary{ false };

      BlockSettle::Common::HDWalletData toCommonMessage() const;
      static HDWalletData fromCommonMessage(const BlockSettle::Common::HDWalletData &);
   };

   struct AddressData
   {
      std::string index;
      bs::Address address;
      std::string comment;
   };

   struct TxCommentData
   {
      BinaryData  txHash;
      std::string comment;
   };

   struct WalletData
   {
      static WalletData fromPbMessage(const Blocksettle::Communication::headless::SyncWalletResponse &);
      static WalletData fromCommonMessage(const BlockSettle::Common::WalletsMessage_WalletData &);
      BlockSettle::Common::WalletsMessage_WalletData toCommonMessage() const;

      //flag value, signifies the higest index entries are unset if not changed from UINT32_MAX
      unsigned int highestExtIndex = UINT32_MAX; 
      unsigned int highestIntIndex = UINT32_MAX;

      std::vector<AddressData>   addresses;
      std::vector<AddressData>   addrPool;
      std::vector<TxCommentData> txComments;
   };

   struct WalletBalanceData
   {
      std::string    id;
      BTCNumericTypes::balance_type balTotal;
      BTCNumericTypes::balance_type balSpendable;
      BTCNumericTypes::balance_type balUnconfirmed;
      uint32_t nbAddresses;

      struct AddressBalance {
         BinaryData  address;
         uint32_t    txn;
         BTCNumericTypes::satoshi_type balTotal;
         BTCNumericTypes::satoshi_type balSpendable;
         BTCNumericTypes::satoshi_type balUnconfirmed;
      };
      std::vector<AddressBalance>   addrBalances;
   };

   struct WatchingOnlyWallet
   {
      struct Address {
         std::string index;
         AddressEntryType  aet;
      };
      struct Leaf {
         std::string          id;
         bs::hd::Path         path;
         BinaryData           publicKey;
         BinaryData           chainCode;
         std::vector<Address> addresses;
      };
      struct Group {
         bs::hd::CoinType  type;
         std::vector<Leaf> leaves;
      };

      NetworkType netType = NetworkType::Invalid;
      std::string id;
      std::string name;
      std::string description;
      std::vector<Group>   groups;
   };

   struct TXWallet
   {
      BinaryData  txHash;
      std::string walletId;
      int64_t     value;
   };

   struct Address
   {
      bs::Address address;
      std::string index;
      std::string walletId;
   };

   struct AddressDetails
   {
      bs::Address address;
      uint64_t    value;
      std::string valueStr;
      std::string walletName;
      TXOUT_SCRIPT_TYPE type;
      BinaryData  outHash;
      int         outIndex;
   };

   struct TXWalletDetails
   {
      BinaryData  txHash;
      std::string walletId;
      std::string walletName;
      bs::core::wallet::Type  walletType;
      Transaction::Direction  direction;
      std::string comment;
      bool        isValid;
      std::string amount;
      std::vector<bs::Address>   outAddresses;
      std::vector<AddressDetails>   inputAddresses;
      std::vector<AddressDetails>   outputAddresses;
      AddressDetails changeAddress;
      Tx    tx;
   };

   Blocksettle::Communication::headless::SyncWalletInfoResponse
      exportHDWalletsInfoToPbMessage(const std::shared_ptr<bs::core::WalletsManager> &);
   Blocksettle::Communication::headless::SyncWalletResponse
      exportHDLeafToPbMessage(const std::shared_ptr<bs::core::hd::Leaf> &leaf);

   bs::wallet::EncryptionType mapFrom(const Blocksettle::Communication::headless::EncryptionType &);
   NetworkType mapFrom(const Blocksettle::Communication::headless::NetworkType &);
   bs::sync::WalletFormat mapFrom(const Blocksettle::Communication::headless::WalletFormat &);

   Blocksettle::Communication::headless::EncryptionType mapFrom(bs::wallet::EncryptionType);
   Blocksettle::Communication::headless::NetworkType mapFrom(NetworkType);

}  //namespace sync
} // bs

#endif
