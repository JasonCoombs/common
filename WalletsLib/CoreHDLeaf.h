/*

***********************************************************************************
* Copyright (C) 2019 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_CORE_HD_LEAF_H
#define BS_CORE_HD_LEAF_H

#include <functional>
#include <unordered_map>
#include <lmdbpp.h>
#include "CoreWallet.h"
#include "HDPath.h"
#include "Wallets/WalletFileInterface.h"

#define LEAF_KEY              0x00002001
#define AUTH_LEAF_KEY         0x00002002
#define SETTLEMENT_LEAF_KEY   0x00002003

namespace spdlog {
   class logger;
}

namespace bs {
   class TxAddressChecker;

   namespace core {
      namespace hd {
         class AuthGroup;
         class Group;
         class Wallet;
         class SettlementLeaf;

         class Leaf : public bs::core::Wallet
         {
            friend class hd::Group;
            friend class hd::Wallet;
            friend class SettlementLeaf;

         public:
            Leaf(NetworkType netType, std::shared_ptr<spdlog::logger> logger,
               wallet::Type type = wallet::Type::Bitcoin);
            ~Leaf();

            virtual void init(
               std::shared_ptr<Armory::Wallets::AssetWallet_Single>,
               const Armory::Wallets::AccountKeyType&);
            virtual std::shared_ptr<hd::Leaf> getCopy(
               std::shared_ptr<Armory::Wallets::AssetWallet_Single>) const = 0;

            void setPath(const bs::hd::Path&);

            std::string walletId() const override;
            std::string shortName() const override { return suffix_; }
            wallet::Type type() const override { return type_; }
            bool isWatchingOnly() const override;
            bool hasExtOnlyAddresses() const override;
            NetworkType networkType(void) const override { return netType_; }

            bool containsAddress(const bs::Address &addr) override;
            bool containsHiddenAddress(const bs::Address &addr) const override;
            Armory::Wallets::AccountKeyType getRootId() const override;

            std::vector<bs::Address> getPooledAddressList() const override;
            std::vector<bs::Address> getExtAddressList() const override;
            std::vector<bs::Address> getIntAddressList() const override;

            size_t getExtAddressCount() const override;
            size_t getUsedAddressCount() const override;
            size_t getIntAddressCount() const override;

            bool isExternalAddress(const Address &) const override;
            bs::Address getNewExtAddress() override;
            bs::Address getNewIntAddress() override;
            bs::Address getNewChangeAddress() override;
            std::shared_ptr<AddressEntry> getAddressEntryForAddr(const BinaryData &addr) override;

            std::string getAddressIndex(const bs::Address &) override;
            bs::hd::Path::Elem getAddressIndexForAddr(const BinaryData &addr) const;
            bs::hd::Path::Elem addressIndex(const bs::Address &addr) const;

            std::pair<bs::Address, bool> synchronizeUsedAddressChain(const std::string&) override;

            bs::Address getAddressByIndex(int index, bool ext) const;

            SecureBinaryData getPublicKeyFor(const bs::Address &) override;
            std::shared_ptr<Armory::Signer::ResolverFeed> getResolver(void) const override;
            std::shared_ptr<Armory::Signer::ResolverFeed> getPublicResolver(void) const override;
            ReentrantLock lockDecryptedContainer() override;

            const bs::hd::Path &path() const { return path_; }
            bs::hd::Path::Elem index() const { return static_cast<bs::hd::Path::Elem>(path_.get(-1)); }
            virtual std::set<AddressEntryType> addressTypes() const = 0;
            virtual AddressEntryType defaultAddressType() const = 0;
            virtual BinaryData serialize() const;

            static std::pair<std::shared_ptr<hd::Leaf>, Armory::Wallets::AccountKeyType> deserialize(
               const BinaryData &ser, NetworkType netType, std::shared_ptr<spdlog::logger> logger);

            void shutdown(void) override;
            std::string getFilename(void) const override;
            std::vector<bs::Address> extendAddressChain(unsigned count, bool extInt) override;

            std::map<BinaryData, bs::hd::Path> indexPath(const std::set<BinaryData>&) override;
            bool hasBip32Path(const Armory::Signer::BIP32_AssetPath&) const override;

            bs::hd::Path::Elem getExtPath(void) const override { return addrTypeExternal_; }
            bs::hd::Path::Elem getIntPath(void) const override { return addrTypeInternal_; }

            std::shared_ptr<Armory::Assets::AssetEntry> getRootAsset(void) const;

         public:
            static const bs::hd::Path::Elem  addrTypeExternal_ = 0u;
            static const bs::hd::Path::Elem  addrTypeInternal_ = 1u;

         protected:
            void reset();

            void readMetaData();
            std::shared_ptr<Armory::Wallets::IO::WalletIfaceTransaction> getDBWriteTx() override;
            std::shared_ptr<Armory::Wallets::IO::WalletIfaceTransaction> getDBReadTx() override;

            bs::Address newAddress(const std::shared_ptr<Armory::Wallets::IO::WalletDBInterface>&);
            bs::Address newInternalAddress(const std::shared_ptr<Armory::Wallets::IO::WalletDBInterface>&);

            bs::hd::Path getPathForAddress(const bs::Address &) const;

            struct AddrPoolKey {
               bs::hd::Path      path;

               bool operator==(const AddrPoolKey &other) const {
                  return (path == other.path);
               }
            };
            using PooledAddress = std::pair<AddrPoolKey, bs::Address>;

         protected:
            mutable std::string     walletId_;
            wallet::Type            type_;
            bs::hd::Path            path_;
            std::string             suffix_;
            const NetworkType       netType_;
            std::shared_ptr<Armory::Accounts::AddressAccount>     accountPtr_;
            std::shared_ptr<Armory::Wallets::AssetWallet_Single>  walletPtr_;

         private:
            void topUpAddressPool(size_t count, bool intExt);
            bs::hd::Path::Elem getLastAddrPoolIndex() const;
         };


         class LeafNative : public Leaf
         {
         public:
            LeafNative(NetworkType netType, std::shared_ptr<spdlog::logger> logger,
               wallet::Type type = wallet::Type::Bitcoin)
               : Leaf(netType, logger, type) {}

            std::shared_ptr<hd::Leaf> getCopy(std::shared_ptr<Armory::Wallets::AssetWallet_Single>) const override;

            std::set<AddressEntryType> addressTypes() const override { return {AddressEntryType_P2WPKH}; }
            AddressEntryType defaultAddressType() const override { return AddressEntryType_P2WPKH; }
         };


         class LeafNested : public Leaf
         {
         public:
            LeafNested(NetworkType netType, std::shared_ptr<spdlog::logger> logger)
               : Leaf(netType, logger, wallet::Type::Bitcoin) {}

            std::shared_ptr<hd::Leaf> getCopy(std::shared_ptr<Armory::Wallets::AssetWallet_Single>) const override;

            std::set<AddressEntryType> addressTypes() const override {
               return { static_cast<AddressEntryType>(AddressEntryType_P2SH | AddressEntryType_P2WPKH) };
            }
            AddressEntryType defaultAddressType() const override {
               return static_cast<AddressEntryType>(AddressEntryType_P2SH | AddressEntryType_P2WPKH);
            }
         };


         class LeafNonSW : public Leaf
         {
         public:
            LeafNonSW(NetworkType netType, std::shared_ptr<spdlog::logger> logger)
               : Leaf(netType, logger, wallet::Type::Bitcoin) {}

            std::shared_ptr<hd::Leaf> getCopy(std::shared_ptr<Armory::Wallets::AssetWallet_Single>) const override;

            std::set<AddressEntryType> addressTypes() const override { return {AddressEntryType_P2PKH}; }
            AddressEntryType defaultAddressType() const override { return AddressEntryType_P2PKH; }
         };


         class LeafArmoryWallet : public Leaf
         {
         public:
            LeafArmoryWallet(NetworkType netType, std::shared_ptr<spdlog::logger> logger)
               : Leaf(netType, logger, wallet::Type::Bitcoin) {}

            std::shared_ptr<hd::Leaf> getCopy(std::shared_ptr<Armory::Wallets::AssetWallet_Single>) const override;

            std::set<AddressEntryType> addressTypes() const override;
            AddressEntryType defaultAddressType() const override;
         };


         class AuthLeaf : public LeafNative
         {
            friend class hd::Leaf;
            friend class hd::AuthGroup;

         private:
            SecureBinaryData salt_;

         private:
            void setSalt(const SecureBinaryData& salt);

         public:
            AuthLeaf(NetworkType netType, std::shared_ptr<spdlog::logger> logger);

            std::shared_ptr<hd::Leaf> getCopy(
               std::shared_ptr<Armory::Wallets::AssetWallet_Single>) const override;
            BinaryData serialize() const override;

            const SecureBinaryData& getSalt(void) const { return salt_; }
         };


         class CCLeaf : public LeafNative
         {
         public:
            CCLeaf(NetworkType netType, std::shared_ptr<spdlog::logger> logger)
               : LeafNative(netType, logger, wallet::Type::ColorCoin) {}
            ~CCLeaf() override = default;

            wallet::Type type() const override { return wallet::Type::ColorCoin; }
         };


         class SettlementLeaf : public LeafNative
         {
         public:
            SettlementLeaf(NetworkType netType, std::shared_ptr<spdlog::logger> logger)
               : LeafNative(netType, logger, wallet::Type::ColorCoin) {}
            ~SettlementLeaf() override = default;

            BinaryData serialize() const override;

            wallet::Type type() const override { return wallet::Type::Settlement; }
            unsigned addSettlementID(const SecureBinaryData&);

            BinaryData signTXRequest(const wallet::TXSignRequest &
               , bool keepDuplicatedRecipients = false) override
            {
               throw std::runtime_error("invalid for settlement leaves, \
                  use bs::core::hd::Wallet::signSettlementTXRequest");
            }

            Armory::Wallets::AssetKeyType getIndexForSettlementID(const SecureBinaryData&) const;
         };

      }  //namespace hd
   }  //namespace core
}  //namespace bs

#endif //BS_CORE_HD_LEAF_H
