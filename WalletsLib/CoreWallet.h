/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_CORE_WALLET_H
#define BS_CORE_WALLET_H

#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <lmdbpp.h>
#include "Address.h"
#include "Wallets/Assets.h"
#include "BtcDefinitions.h"
#include "CheckRecipSigner.h"
#include "EasyCoDec.h"
#include "Script.h"
#include "Signer.h"
#include "TxClasses.h"
#include "WalletEncryption.h"
#include "Wallets/Wallets.h"
#include "Wallets/WalletIdTypes.h"
#include "BIP32_Node.h"
#include "HDPath.h"

#define WALLETNAME_KEY           0x00000020
#define WALLETDESCRIPTION_KEY    0x00000021
#define WALLET_EXTONLY_KEY       0x00000030
#define WALLET_PWD_META_KEY      0x00000031
#define CHAT_NODE_KEY            0x00000040

#define BS_WALLET_DBNAME   "bs_wallet_db"
#define BS_CHAT_DBNAME     "bs_chat_db"

namespace spdlog {
   class logger;
}

class ArmoryConnection;

namespace bs {
   namespace sync {
      enum class SyncState
      {
         Success,
         NothingToDo,
         Failure
      };
   }

   namespace core {
      class Wallet;
      namespace wallet {
         class AssetEntryMeta : public Armory::Assets::AssetEntry
         {
         public:
            enum Type {
               Unknown = 0,
               Comment = 4,
               Settlement = 5,
               SettlementCP = 6
            };
            AssetEntryMeta(Type type, Armory::Wallets::AssetId id)
               : Armory::Assets::AssetEntry(Armory::Assets::AssetEntryType_Single, id)
               , type_(type) {}
            virtual ~AssetEntryMeta() = default;

            Type type() const { return type_; }
            virtual BinaryData key() const {
               BinaryWriter bw;
               bw.put_uint8_t(ASSETENTRY_PREFIX);
               bw.put_int32_t(ID_.getAssetKey());
               return bw.getData();
            }
            static std::shared_ptr<AssetEntryMeta> deserialize(int index, BinaryDataRef value);
            virtual bool deserialize(BinaryRefReader brr) = 0;

            bool hasPrivateKey(void) const override { return false; }
            const Armory::Wallets::EncryptionKeyId&
               getPrivateEncryptionKeyId(void) const override { return {}; }

         private:
            Type  type_;
         };

         class AssetEntryComment : public AssetEntryMeta
         {
            BinaryData  key_;
            std::string comment_;
         public:
            AssetEntryComment(Armory::Wallets::AssetId id, const BinaryData &key
               , const std::string &comment)
               : AssetEntryMeta(AssetEntryMeta::Comment, id), key_(key), comment_(comment) {}
            AssetEntryComment() : AssetEntryMeta(AssetEntryMeta::Comment, {}) {}

            BinaryData key() const override { return key_; }
            const std::string &comment() const { return comment_; }
            BinaryData serialize() const override;
            bool deserialize(BinaryRefReader brr) override;
         };


         struct HwWalletInfo {
            bs::wallet::HardwareEncKey::WalletType type;
            std::string vendor;
            std::string label;
            std::string deviceId;

            std::string xpubRoot;
            std::string xpubNestedSegwit;
            std::string xpubNativeSegwit;
            std::string xpubLegacy;
         };

         class AssetEntrySettlement : public AssetEntryMeta // For saving own auth address for settlement
         {
            BinaryData  settlementId_;
            bs::Address authAddr_;

         public:
            AssetEntrySettlement(Armory::Wallets::AssetId id, const BinaryData &settlId
               , const bs::Address &authAddr)
               : AssetEntryMeta(AssetEntryMeta::Settlement, id), settlementId_(settlId)
               , authAddr_(authAddr)
            {
               if (settlId.getSize() != 32) {
                  throw std::invalid_argument("wrong settlementId size");
               }
               if (!authAddr.isValid()) {
                  throw std::invalid_argument("invalid auth address");
               }
            }
            AssetEntrySettlement() : AssetEntryMeta(AssetEntryMeta::Settlement, {}) {}

            BinaryData key() const override { return settlementId_; }
            bs::Address address() const { return authAddr_; }
            BinaryData serialize() const override;
            bool deserialize(BinaryRefReader brr) override;
         };

         class AssetEntrySettlCP : public AssetEntryMeta // For saving settlement id and counterparty pubkey by payin hash
         {
            BinaryData  txHash_;
            BinaryData  settlementId_;
            BinaryData  cpPubKey_;

         public:
            AssetEntrySettlCP(Armory::Wallets::AssetId id, const BinaryData &payinHash
               , const BinaryData &settlementId, const BinaryData &cpPubKey)
               : AssetEntryMeta(AssetEntryMeta::SettlementCP, id), txHash_(payinHash)
               , settlementId_(settlementId), cpPubKey_(cpPubKey)
            {
               if (payinHash.getSize() != 32) {
                  throw std::invalid_argument("wrong payin hash size");
               }
               if (settlementId.getSize() != 32) {
                  throw std::invalid_argument("wrong settlementId size");
               }
            }
            AssetEntrySettlCP() : AssetEntryMeta(AssetEntryMeta::SettlementCP, {}) {}

            BinaryData key() const override { return txHash_; }
            BinaryData settlementId() const { return settlementId_; }
            BinaryData cpPubKey() const { return cpPubKey_; }
            BinaryData serialize() const override;
            bool deserialize(BinaryRefReader brr) override;
         };

         class MetaData
         {
            std::map<BinaryData, std::shared_ptr<AssetEntryMeta>>   data_;

         protected:
            std::atomic_uint  nbMetaData_{ 0 };

            MetaData() : nbMetaData_(0) {}

            std::shared_ptr<AssetEntryMeta> get(const BinaryData &key) const {
               const auto itData = data_.find(key);
               if (itData != data_.end()) {
                  return itData->second;
               }
               return nullptr;
            }
            void set(const std::shared_ptr<AssetEntryMeta> &value);
            bool write(const std::shared_ptr<Armory::Wallets::IO::WalletIfaceTransaction> &);
            void readFromDB(const std::shared_ptr<Armory::Wallets::IO::WalletIfaceTransaction> &);
            std::map<BinaryData, std::shared_ptr<AssetEntryMeta>> fetchAll() const { return data_; }
         };

         struct Comment
         {
            enum Type {
               ChangeAddress,
               AuthAddress,
               SettlementPayOut
            };
            static const char *toString(Type t)
            {
               switch (t)
               {
               case ChangeAddress:     return "--== Change Address ==--";
               case AuthAddress:       return "--== Auth Address ==--";
               case SettlementPayOut:  return "--== Settlement Pay-Out ==--";
               default:                return "";
               }
            }
         };

         class Seed
         {
         public:
            Seed(NetworkType netType) :
               seed_(SecureBinaryData()), netType_(netType)
            {}

            Seed(const SecureBinaryData &seed, NetworkType netType);

            bool empty() const { return seed_.empty(); }
            bool hasPrivateKey() const { return node_.getPrivateKey().getSize() == 32; }
            const SecureBinaryData &privateKey() const { return node_.getPrivateKey(); }
            const SecureBinaryData &seed() const { return seed_; }
            NetworkType networkType() const { return netType_; }
            void setNetworkType(NetworkType netType) { netType_ = netType; walletId_.clear(); }
            std::string getWalletId() const;

            EasyCoDec::Data toEasyCodeChecksum(size_t ckSumSize = 2) const;
            static SecureBinaryData decodeEasyCodeChecksum(const EasyCoDec::Data &, size_t ckSumSize = 2);
            static BinaryData decodeEasyCodeLineChecksum(const std::string&easyCodeHalf, size_t ckSumSize = 2, size_t keyValueSize = 16);
            static Seed fromEasyCodeChecksum(const EasyCoDec::Data &, NetworkType, size_t ckSumSize = 2);
            static Seed fromBip39(const std::string& sentence,
               NetworkType netType, const std::vector<std::vector<std::string>>& dictionaries);

            SecureBinaryData toXpriv(void) const;
            static Seed fromXpriv(const SecureBinaryData&, NetworkType);
            const BIP32_Node& getNode(void) const { return node_; }

         private:
            BIP32_Node node_;
            SecureBinaryData seed_;
            NetworkType       netType_ = NetworkType::Invalid;
            mutable std::string  walletId_;
         };

         enum class Type {
            Unknown,
            Bitcoin,
            ColorCoin,
            Authentication,
            Settlement
         };

         struct TXSignRequest
         {
            std::vector<std::string>   walletIds;
            struct {
               bs::Address address;
               std::string index;
               uint64_t    value{ 0 };
            }  change;
            uint64_t    fee{ 0 };
            bool        RBF{ false };
            BinaryData  serializedTx;

            std::string comment;
            // true for normal transactions, false for offline OTC
            bool allowBroadcasts{false};
            // timestamp when settlement TX sign expires
            std::chrono::system_clock::time_point expiredTimestamp{};
            BinaryData txHash;

            Armory::Signer::Signer armorySigner_;

            TXSignRequest() {}
            TXSignRequest(const TXSignRequest &other)
            {
               *this = other;
            }

            TXSignRequest &operator=(const TXSignRequest &other)
            {
               if (&other == this) {
                  return *this;
               }

               armorySigner_ = other.armorySigner_;
               walletIds = other.walletIds;
               change = other.change;
               fee = other.fee;
               RBF = other.RBF;
               comment = other.comment;
               allowBroadcasts = other.allowBroadcasts;
               expiredTimestamp = other.expiredTimestamp;
               txHash = other.txHash;
               return *this;
            }
            bool isValid() const noexcept;
            Codec_SignerState::SignerState serializeState(void) const {
               return armorySigner_.serializeState();
            }
            BinaryData txId(const std::shared_ptr<Armory::Signer::ResolverFeed> &resolver=nullptr) {
               if (resolver != nullptr) {
                  armorySigner_.resetFeed();
                  armorySigner_.setFeed(resolver);
               }
               return armorySigner_.getTxId();
            }
            void resolveSpenders(const std::shared_ptr<Armory::Signer::ResolverFeed> &resolver = nullptr) {
               if (resolver != nullptr) {
                  armorySigner_.resetFeed();
                  armorySigner_.setFeed(resolver);
               }
               armorySigner_.resolvePublicData();
            }
            size_t estimateTxVirtSize() const;

            using ContainsAddressCb = std::function<bool(const bs::Address &address)>;
            uint64_t amount(const ContainsAddressCb &containsAddressCb) const;
            uint64_t inputAmount(const ContainsAddressCb &containsAddressCb) const;
            uint64_t totalSpent(const ContainsAddressCb &containsAddressCb) const;
            uint64_t changeAmount(const ContainsAddressCb &containsAddressCb) const;

            uint64_t amountReceived(const ContainsAddressCb &containsAddressCb) const;
            uint64_t amountSent(const ContainsAddressCb &containsAddressCb) const;

            uint64_t amountReceivedOn(const bs::Address &address, bool removeDuplicatedRecipients = false) const;

            uint64_t getFee() const;

            std::vector<UTXO> getInputs(const ContainsAddressCb &containsAddressCb) const;
            std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> getRecipients(const ContainsAddressCb &containsAddressCb) const;

            bool isSourceOfTx(const Tx &signedTx) const;

            void DebugPrint(const std::string& prefix, const std::shared_ptr<spdlog::logger>&
               , bool serializeAndPrint, const std::shared_ptr<Armory::Signer::ResolverFeed> &resolver=nullptr);

         private:
            Armory::Signer::Signer& getSigner(void);
         };


         struct TXMultiSignRequest
         {
            std::set<std::string>   walletIDs_;
            Armory::Signer::Signer  armorySigner_;
            bool RBF;

            bool isValid() const noexcept;
            void addWalletId(const std::string &walletId)
            { walletIDs_.insert(walletId); }
         };


         struct SettlementData
         {
            BinaryData  settlementId;
            BinaryData  cpPublicKey;
            bool        ownKeyFirst = true;
         };

         BinaryData computeID(const BinaryData &input);

      }  // namepsace wallet


      struct KeyPair
      {
         SecureBinaryData  privKey;
         BinaryData        pubKey;
      };


      using InputSigs = std::map<unsigned int, BinaryData>;
      class Wallet : protected wallet::MetaData   // Abstract parent for generic wallet classes
      {
      public:
         Wallet(std::shared_ptr<spdlog::logger> logger);
         virtual ~Wallet();

         virtual std::string walletId() const { return "defaultWalletID"; }
         virtual std::string name() const { return walletName_; }
         virtual std::string shortName() const { return name(); }
         virtual wallet::Type type() const { return wallet::Type::Bitcoin; }

         bool operator ==(const Wallet &w) const { return (w.walletId() == walletId()); }
         bool operator !=(const Wallet &w) const { return (w.walletId() != walletId()); }

         virtual bool containsAddress(const bs::Address &addr) = 0;
         virtual bool containsHiddenAddress(const bs::Address &) const { return false; }
         virtual Armory::Wallets::AddressAccountId getRootId() const = 0;
         virtual NetworkType networkType(void) const = 0;

         virtual bool isWatchingOnly() const = 0;
         virtual bool hasExtOnlyAddresses() const { return false; }

         virtual std::string getAddressComment(const bs::Address& address) const;
         virtual bool setAddressComment(const bs::Address &addr, const std::string &comment);
         virtual std::string getTransactionComment(const BinaryData &txHash);
         virtual bool setTransactionComment(const BinaryData &txHash, const std::string &comment);
         virtual std::vector<std::pair<BinaryData, std::string>> getAllTxComments() const;
         bool setSettlementMeta(const BinaryData &settlementId, const bs::Address &authAddr);
         bs::Address getSettlAuthAddr(const BinaryData &settlementId);
         bool setSettlCPMeta(const BinaryData &payinHash, const BinaryData &settlementId
            , const BinaryData &cpPubKey);
         std::pair<BinaryData, BinaryData> getSettlCP(const BinaryData &txHash);

         virtual std::vector<bs::Address> getUsedAddressList() const {
            return usedAddresses_;
         }
         virtual std::vector<bs::Address> getPooledAddressList() const { return {}; }
         virtual std::vector<bs::Address> getExtAddressList() const { return usedAddresses_; }
         virtual std::vector<bs::Address> getIntAddressList() const { return usedAddresses_; }
         virtual bool isExternalAddress(const Address &) const { return true; }
         virtual size_t getUsedAddressCount() const { return usedAddresses_.size(); }
         virtual size_t getExtAddressCount() const { return usedAddresses_.size(); }
         virtual size_t getIntAddressCount() const { return usedAddresses_.size(); }
         virtual size_t getWalletAddressCount() const { return addrCount_; }

         virtual bs::Address getNewExtAddress() = 0;
         virtual bs::Address getNewIntAddress() = 0;
         virtual bs::Address getNewChangeAddress() { return getNewIntAddress(); }
         virtual std::shared_ptr<AddressEntry> getAddressEntryForAddr(const BinaryData &addr) = 0;
         virtual std::string getAddressIndex(const bs::Address &) = 0;

         virtual bs::hd::Path::Elem getExtPath(void) const = 0;
         virtual bs::hd::Path::Elem getIntPath(void) const = 0;

         /***
         Used to keep track of sync wallet used address index increments on the
         Armory wallet side
         ***/
         virtual std::pair<bs::Address, bool> synchronizeUsedAddressChain(
            const std::string&) = 0;

         /***
         Called by the sign container in reponse to sync wallet's topUpAddressPool
         Will result in public address chain extention on the relevant Armory address account
         ***/
         virtual std::vector<bs::Address> extendAddressChain(unsigned count, bool extInt) = 0;

         virtual std::shared_ptr<Armory::Signer::ResolverFeed> getResolver(void) const = 0;
         virtual std::shared_ptr<Armory::Signer::ResolverFeed> getPublicResolver(void) const = 0;
         virtual ReentrantLock lockDecryptedContainer() = 0;

         virtual BinaryData signTXRequest(const wallet::TXSignRequest &
            , bool keepDuplicatedRecipients = false);
         virtual Codec_SignerState::SignerState signPartialTXRequest(const wallet::TXSignRequest &);

         virtual BinaryData signTXRequestWithWitness(const wallet::TXSignRequest &
            , const InputSigs &);

         virtual SecureBinaryData getPublicKeyFor(const bs::Address &) = 0;
         virtual SecureBinaryData getPubChainedKeyFor(const bs::Address &addr) { return getPublicKeyFor(addr); }

         //shutdown db containers, typically prior to deleting the wallet file
         virtual void shutdown(void) = 0;
         virtual std::string getFilename(void) const = 0;

         //find the path for a set of prefixed scrAddr
         virtual std::map<BinaryData, bs::hd::Path> indexPath(const std::set<BinaryData>&) = 0;
         virtual bool hasBip32Path(const Armory::Signer::BIP32_AssetPath&) const = 0;

         Armory::Signer::Signer getSigner(const wallet::TXSignRequest &,
            bool keepDuplicatedRecipients = false);

      protected:
         virtual std::shared_ptr<Armory::Wallets::IO::WalletIfaceTransaction> getDBWriteTx() = 0;
         virtual std::shared_ptr<Armory::Wallets::IO::WalletIfaceTransaction> getDBReadTx() = 0;

      protected:
         std::string       walletName_;
         std::shared_ptr<spdlog::logger>   logger_; // May need to be set manually.
         mutable std::vector<bs::Address>       usedAddresses_;
         mutable std::set<BinaryData>           addressHashes_;
         size_t            addrCount_ = 0;
      };

      using WalletMap = std::unordered_map<std::string, std::shared_ptr<Wallet>>;   // key is wallet id
      BinaryData SignMultiInputTX(const wallet::TXMultiSignRequest &
         , const WalletMap &, bool partial = false);
      BinaryData SignMultiInputTXWithWitness(const wallet::TXMultiSignRequest &
         , const WalletMap &, const InputSigs &);
   }  //namespace core
}  //namespace bs

#endif //BS_CORE_WALLET_H
