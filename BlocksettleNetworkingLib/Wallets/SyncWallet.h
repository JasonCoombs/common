/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_SYNC_WALLET_H
#define BS_SYNC_WALLET_H

#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <QString>
#include "Address.h"
#include "ArmoryConnection.h"
#include "Assets.h"
#include "AsyncClient.h"
#include "BtcDefinitions.h"
#include "CoreWallet.h"
#include "LedgerEntry.h"
#include "UtxoReservation.h"
#include "ValidityFlag.h"
#include "WalletEncryption.h"

namespace spdlog {
   class logger;
}
class WalletSignerContainer;

namespace bs {
   namespace sync {
      class CCDataResolver
      {
      public:
         virtual ~CCDataResolver() = default;
         virtual std::string nameByWalletIndex(bs::hd::Path::Elem) const = 0;
         virtual uint64_t lotSizeFor(const std::string &cc) const = 0;
         virtual bs::Address genesisAddrFor(const std::string &cc) const = 0;
         virtual std::vector<std::string> securities() const = 0;
      };

      enum class TxValidity : int
      {
         Unknown,
         Valid,
         Invalid,
      };

      class Wallet;

      namespace wallet {

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
               }
               return "";
            }
         };

         // if there is change then changeAddr must be set.
         // inputIndices required for HW wallets only.
         bs::core::wallet::TXSignRequest createTXRequest(const std::vector<std::string> &walletsIds
            , const std::vector<UTXO> &inputs
            , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &
            , bool allowBroadcasts
            , const bs::Address &changeAddr = {}
            , const std::string &changeIndex = {}
            , const uint64_t fee = 0, bool isRBF = false);

         bs::core::wallet::TXSignRequest createTXRequest(const std::vector<Wallet*>& wallets
            , const std::vector<UTXO>& inputs
            , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>>& recipients
            , bool allowBroadcasts
            , const bs::Address& changeAddr
            , const uint64_t fee, bool isRBF);

         bs::core::wallet::TXSignRequest createTXRequest(const std::vector<std::shared_ptr<Wallet>> &wallets
            , const std::vector<UTXO> &inputs
            , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &
            , bool allowBroadcasts
            , const bs::Address &changeAddr = {}
            , const uint64_t fee = 0, bool isRBF = false);

      }  // namepsace wallet

      class WalletACT;
      class WalletCallbackTarget;

      using RecipientMap = std::map<unsigned, std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>>>;

      class Wallet
      {
         friend class WalletACT;

      public:
         Wallet(WalletSignerContainer *, const std::shared_ptr<spdlog::logger> &logger = nullptr);
         virtual ~Wallet();

         using CbAddress = std::function<void(const bs::Address &)>;
         using CbAddresses = std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>;

         virtual void synchronize(const std::function<void()> &cbDone);

         virtual std::string walletId(void) const = 0;
         virtual std::string walletIdInt(void) const;

         virtual std::string name() const { return walletName_; }
         virtual std::string shortName() const { return name(); }
         virtual std::string description() const = 0;
         virtual void setDescription(const std::string &) = 0;
         virtual core::wallet::Type type() const { return core::wallet::Type::Bitcoin; }
         virtual bool hasId(const std::string &id) const { return (walletId() == id); }
         virtual bool hasScanId(const std::string&) const { return false; }

         virtual void setArmory(const std::shared_ptr<ArmoryConnection> &);
         virtual void setUserId(const BinaryData &) {}
         void setTrackLiveAddresses(bool flag) { trackLiveAddresses_ = flag; }

         bool operator ==(const Wallet &w) const { return (w.walletId() == walletId()); }
         bool operator !=(const Wallet &w) const { return (w.walletId() != walletId()); }

         virtual bool containsAddress(const bs::Address &addr) = 0;
         virtual bool containsHiddenAddress(const bs::Address &) const { return false; }

         using WalletRegData = std::unordered_map<std::string, std::vector<BinaryData>>;
         virtual WalletRegData regData() const;
         virtual void onRegistered();

         using UnconfTgtData = std::unordered_map<std::string, unsigned int>;
         virtual UnconfTgtData unconfTargets() const;

         virtual std::vector<std::string> internalIds() const { return { walletId() }; }

         virtual bool isBalanceAvailable() const;
         void onBalanceAvailable(const std::function<void()> &) const;
         virtual BTCNumericTypes::balance_type getSpendableBalance() const;
         virtual BTCNumericTypes::balance_type getUnconfirmedBalance() const;
         virtual BTCNumericTypes::balance_type getTotalBalance() const;
         virtual void init(bool force = false);

         virtual std::vector<uint64_t> getAddrBalance(const bs::Address &addr) const;
         virtual uint64_t getAddrTxN(const bs::Address &addr) const;

         virtual bool isWatchingOnly() const { return false; }
         virtual std::vector<bs::wallet::EncryptionType> encryptionTypes() const { return {}; }
         virtual std::vector<BinaryData> encryptionKeys() const { return {}; }
         virtual bs::wallet::KeyRank encryptionRank() const { return { 1, 1 }; }
         virtual bool hasExtOnlyAddresses() const { return false; }
         virtual std::string getAddressComment(const bs::Address& address) const;
         virtual bool setAddressComment(const bs::Address &addr, const std::string &comment, bool sync = true);
         virtual std::string getTransactionComment(const BinaryData &txHash);
         virtual bool setTransactionComment(const BinaryData &txOrHash, const std::string &comment, bool sync = true);

         std::set<BinaryData> allAddresses() const { return registeredAddresses_; }
         virtual std::vector<bs::Address> getUsedAddressList() const { return usedAddresses_; }
         virtual std::vector<bs::Address> getExtAddressList() const { return usedAddresses_; }
         virtual std::vector<bs::Address> getIntAddressList() const { return usedAddresses_; }
         virtual std::vector<std::pair<bs::Address, std::string>> getAddressPool() const { return {}; }
         virtual bool isExternalAddress(const bs::Address &) const { return true; }
         virtual size_t getUsedAddressCount() const { return usedAddresses_.size(); }
         virtual size_t getExtAddressCount() const { return usedAddresses_.size(); }
         virtual size_t getIntAddressCount() const { return usedAddresses_.size(); }
         virtual size_t getWalletAddressCount() const { return balanceData_ ? balanceData_->addrCount : 0; }

         virtual void getNewExtAddress(const CbAddress &) = 0;
         virtual void getNewIntAddress(const CbAddress &) = 0;
         virtual void getNewChangeAddress(const CbAddress &cb) { getNewExtAddress(cb); }

         virtual std::string getAddressIndex(const bs::Address &) = 0;
         virtual std::string getWalletIdForAddress(const bs::Address &) const { return walletId(); }

         virtual std::shared_ptr<Armory::Signer::ResolverFeed> getPublicResolver() const = 0;

         //Adds an arbitrary address identified by index
         virtual int addAddress(const bs::Address &, const std::string &index, bool sync = true);

         virtual BTCNumericTypes::balance_type getTxBalance(int64_t val) const { return val / BTCNumericTypes::BalanceDivider; }
         virtual QString displayTxValue(int64_t val) const;
         virtual QString displaySymbol() const { return QLatin1String("XBT"); }
         virtual TxValidity isTxValid(const BinaryData &) const { return TxValidity::Valid; }

         // changeAddress must be set if there is change
         virtual core::wallet::TXSignRequest createTXRequest(const std::vector<UTXO> &
            , const std::vector<std::shared_ptr<Armory::Signer::ScriptRecipient>> &
            , bool allowBroadcasts
            , const uint64_t fee = 0, bool isRBF = false
            , const bs::Address &changeAddress = {});
         virtual core::wallet::TXSignRequest createPartialTXRequest(uint64_t spendVal
            , const std::vector<UTXO> &inputs
            , std::pair<bs::Address, unsigned> changePair = {bs::Address(), UINT32_MAX}
            , float feePerByte = 0
            , const RecipientMap &recipients = {}
            , const Codec_SignerState::SignerState &prevPart = {}
            , unsigned assumedRecipientCount = UINT32_MAX);

         virtual bool deleteRemotely() { return false; } //stub
         virtual void merge(const std::shared_ptr<Wallet> &) {};

         [[deprecated]] void newAddresses(const std::vector<std::string> &inData, const CbAddresses &cb);
         [[deprecated]] void trackChainAddressUse(const std::function<void(bs::sync::SyncState)> &cb);
         [[deprecated]] virtual void scan(const std::function<void(bs::sync::SyncState)> &cb) {}
         size_t getActiveAddressCount(void);

         /***
         Baseline db fetch methods using combined get logic. These come 
         with the default implementation but remain virtual to leave 
         room for custom behavior. 
         ***/

         //balance and count
         [[deprecated]] virtual bool updateBalances(const std::function<void(void)> & = nullptr);
         [[deprecated]] virtual bool getAddressTxnCounts(const std::function<void(void)> &cb = nullptr);
         
         //utxos
         [[deprecated]] virtual bool getSpendableTxOutList(const ArmoryConnection::UTXOsCb &, uint64_t val, bool excludeReservation);
         [[deprecated]] virtual bool getSpendableZCList(const ArmoryConnection::UTXOsCb &) const;
         [[deprecated]] virtual bool getRBFTxOutList(const ArmoryConnection::UTXOsCb &) const;
         [[deprecated]] virtual std::vector<UTXO> getIncompleteUTXOs() const;

         //custom ACT
         template<class U> void setCustomACT(
            const std::shared_ptr<ArmoryConnection> &armory)
         {
            act_ = std::make_unique<U>(armory.get(), this);
            skipPostOnline_ = true;
         }

         void setWCT(WalletCallbackTarget *wct);
         WalletACT* peekACT(void) const { return act_.get(); }

      protected:
         [[deprecated]] virtual void onZeroConfReceived(const std::vector<bs::TXEntry>&);
         [[deprecated]] virtual void onNewBlock(unsigned int, unsigned int);
         [[deprecated]] virtual void onRefresh(const std::vector<BinaryData> &ids, bool online);
         [[deprecated]] virtual void onZCInvalidated(const std::set<BinaryData> &ids);

         virtual std::vector<BinaryData> getAddrHashes() const = 0;

         template <typename MapT> static void updateMap(const MapT &src, MapT &dst)
         {
            for (const auto &elem : src) {     // std::map::insert doesn't replace elements
               dst[elem.first] = std::move(elem.second);
            }
         }

         bool getHistoryPage(const std::shared_ptr<AsyncClient::BtcWallet> &
            , uint32_t id, std::function<void(const Wallet *wallet
               , std::vector<DBClientClasses::LedgerEntry>)>, bool onlyNew = false) const;

      public:
         enum class Registered
         {
            // Wallet is not registered
            Offline,
            // Wallet was successfully registered
            Registered,
            // Wallet was successfully registered before but new registration has been started
            Updating,
         };

         Registered isRegistered(void) const { return isRegistered_; }

      protected:
         std::string                walletName_;
         WalletSignerContainer*     signContainer_;
         std::shared_ptr<ArmoryConnection>   armory_;
         std::atomic_bool                    armorySet_{};
         std::shared_ptr<spdlog::logger>     logger_; // May need to be set manually.
         mutable std::vector<bs::Address>    usedAddresses_;

         mutable std::mutex mutex_;
         std::map<bs::Address, std::string>  addrComments_;
         std::map<BinaryData, std::string>   txComments_;

         std::unique_ptr<WalletACT>   act_;
         WalletCallbackTarget       * wct_ = nullptr;

         ValidityFlag validityFlag_;

         std::map<BinaryData, Tx>   zcEntries_;
         std::vector<UTXO> reservedUTXOs_;

      private:
         std::string regId_;
         mutable std::map<uint32_t, std::vector<DBClientClasses::LedgerEntry>>   historyCache_;
         mutable std::atomic_bool         balThreadRunning_{ false };
         mutable std::condition_variable  balThrCV_;
         mutable std::mutex               balThrMutex_;
         mutable std::vector<std::function<void()>>   cbBalThread_;

      protected:
         bool firstInit_ = false;
         bool skipPostOnline_ = false;
         bool trackLiveAddresses_ = true;
         std::atomic<Registered> isRegistered_{Registered::Offline};

         struct BalanceData {
            std::atomic<BTCNumericTypes::balance_type>   spendableBalance{0};
            std::atomic<BTCNumericTypes::balance_type>   unconfirmedBalance{0};
            std::atomic<BTCNumericTypes::balance_type>   totalBalance{0};

            size_t   addrCount = 0;
            std::mutex  addrMapsMtx;
            std::map<BinaryData, std::vector<uint64_t>>  addressBalanceMap;
            std::map<BinaryData, uint64_t>               addressTxNMap;

            std::mutex  cbMutex;
            std::vector<std::function<void(void)>> cbTxNs;
            std::vector<std::function<void(void)>> cbBalances;
         };
         [[deprecated]] mutable std::shared_ptr<BalanceData>   balanceData_;
         [[deprecated]] std::map<bs::Address, std::function<void(const std::shared_ptr<AsyncClient::LedgerDelegate> &)>>   cbLedgerByAddr_;

         // List of addresses that was actually registered in armory
         mutable std::set<BinaryData>           registeredAddresses_;
      };

      class WalletACT : public ArmoryCallbackTarget
      {
      public:
         WalletACT(Wallet *leaf)
            : parent_(leaf)
         {
         }
         ~WalletACT() override { cleanup(); }
         virtual void onRefresh(const std::vector<BinaryData> &ids, bool online) override {
            parent_->onRefresh(ids, online);
         }
         virtual void onZCReceived(const std::string& , const std::vector<bs::TXEntry>& zcs) override {
            parent_->onZeroConfReceived(zcs);
         }
         virtual void onNewBlock(unsigned int block, unsigned int bh) override {
            parent_->onNewBlock(block, bh);
         }
         virtual void onZCInvalidated(const std::set<BinaryData> &ids) override {
            parent_->onZCInvalidated(ids);
         }
         void onLedgerForAddress(const bs::Address &, const std::shared_ptr<AsyncClient::LedgerDelegate> &) override;
      protected:
         Wallet *parent_;
      };

      class WalletCallbackTarget
      {
      public:  // all virtual methods have empty implementation by default
         virtual ~WalletCallbackTarget() = default;

         virtual void addressAdded(const std::string &) {}
         virtual void walletReady(const std::string &) {}
         virtual void balanceUpdated(const std::string &) {}
         virtual void metadataChanged(const std::string &) {}
         virtual void walletCreated(const std::string &) {}
         virtual void walletDestroyed(const std::string &) {}
         virtual void walletReset(const std::string &) {}
         virtual void scanComplete(const std::string &) {}
      };

      struct Transaction
      {
         enum Direction {
            Unknown,
            Received,
            Sent,
            Internal,
            Auth,
            PayIn,
            PayOut,
            Revoke,
            Delivery,
            Payment
         };

         static const char *toString(Direction dir) {
            switch (dir)
            {
            case Received:    return QT_TR_NOOP("Received");
            case Sent:        return QT_TR_NOOP("Sent");
            case Internal:    return QT_TR_NOOP("Internal");
            case Auth:        return QT_TR_NOOP("AUTHENTICATION");
            case PayIn:       return QT_TR_NOOP("PAY-IN");
            case PayOut:      return QT_TR_NOOP("PAY-OUT");
            case Revoke:      return QT_TR_NOOP("REVOKE");
            case Delivery:    return QT_TR_NOOP("Delivery");
            case Payment:     return QT_TR_NOOP("Payment");
            case Unknown:     return QT_TR_NOOP("Undefined");
            }
            return QT_TR_NOOP("Undefined");
         }
         static const char *toStringDir(Direction dir) {
            switch (dir)
            {
            case Received: return QT_TR_NOOP("Received with");
            case Sent:     return QT_TR_NOOP("Sent to");
            default:       return toString(dir);
            }
         }
      };

   }  //namespace sync
}  //namespace bs

#endif //BS_SYNC_WALLET_H
