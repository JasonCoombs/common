/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WALLETS_ADAPTER_H
#define WALLETS_ADAPTER_H

#include <mutex>
#include "Address.h"
#include "BinaryData.h"
#include "HDPath.h"
#include "SignerClient.h"
#include "Message/ThreadedAdapter.h"
#include "Wallets/SyncWallet.h"

namespace spdlog {
   class logger;
}
namespace BlockSettle {
   namespace Common {
      class ArmoryMessage_AddressTxCountResponse;
      class ArmoryMessage_Transactions;
      class ArmoryMessage_UTXOs;
      class ArmoryMessage_WalletBalanceResponse;
      class ArmoryMessage_ZCReceived;
      class WalletsMessage_AddressComments;
      class WalletsMessage_PayinRequest;
      class WalletsMessage_PayoutRequest;
      class WalletsMessage_ReserveUTXOs;
      class WalletsMessage_ReservationKey;
      class WalletsMessage_TXComment;
      class WalletsMessage_TXDetailsRequest;
      class WalletsMessage_UtxoListRequest;
      class WalletsMessage_WalletAddresses;
      class WalletsMessage_WalletsListRequest;
   }
}
namespace bs {
   namespace sync {
      namespace hd {
         class Group;
         class Wallet;
      }
      class Wallet;
      struct WalletInfo;
   }
   class UtxoReservation;
}

class WalletsAdapter : public bs::message::ThreadedAdapter, public bs::sync::WalletCallbackTarget
{
public:
   WalletsAdapter(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::message::User> &ownUser
      , std::unique_ptr<SignerClient>
      , const std::shared_ptr<bs::message::User> &blockchainUser);
   ~WalletsAdapter() override;

   bool processEnvelope(const bs::message::Envelope &) override;

   std::set<std::shared_ptr<bs::message::User>> supportedReceivers() const override {
      return { ownUser_ };
   }
   std::string name() const override { return "Wallets"; }

protected:
   virtual bool balanceEnabled() const { return true; }
   virtual bool trackLiveAddresses() const { return true; }

   void addressAdded(const std::string &) override;
   void balanceUpdated(const std::string &) override;
   void walletCreated(const std::string &) override;
   void walletDestroyed(const std::string &) override;
   void walletReset(const std::string &) override;
   void scanComplete(const std::string &walletId) override;
   void metadataChanged(const std::string &walletId) override;

private:
   bool processBlockchain(const bs::message::Envelope &env);
   bool processOwnRequest(const bs::message::Envelope &env);

   void reset();
   void startWalletsSync();
   void sendLoadingBC();
   void sendWalletChanged(const std::string &walletId);
   void sendWalletReady(const std::string &walletId);
   void sendWalletError(const std::string &walletId, const std::string &errMsg);
   void authLeafAdded(const std::string& walletId);

   void loadWallet(const bs::sync::WalletInfo &);
   void saveWallet(const std::shared_ptr<bs::sync::hd::Wallet> &);
   std::shared_ptr<bs::sync::hd::Wallet> getHDWalletById(
      const std::string &walletId) const;
   std::shared_ptr<bs::sync::Wallet> getWalletById(const std::string& walletId) const;
   std::shared_ptr<bs::sync::Wallet> getWalletByAddress(const bs::Address &) const;
   std::shared_ptr<bs::sync::hd::Group> getGroupByWalletId(const std::string &) const;
   std::shared_ptr<bs::sync::hd::Wallet> getHDRootForLeaf(const std::string &walletId) const;
   std::shared_ptr<bs::sync::hd::Wallet> getPrimaryWallet() const;
   void eraseWallet(const std::shared_ptr<bs::sync::hd::Wallet>&);
   void eraseWallet(const std::shared_ptr<bs::sync::Wallet> &, bool unregister = true);
   bool isAddressUsed(const bs::Address&, const std::string& walletId = {}) const;

   void addWallet(const std::shared_ptr<bs::sync::Wallet> &);
   void registerWallet(const std::shared_ptr<bs::sync::Wallet> &);
   void scanWallet(const std::shared_ptr<bs::sync::Wallet>&, bool isExt);
   void processScanRegistered(const std::shared_ptr<bs::sync::Wallet>&
      , const std::string &scanId);
   void resumeScan(const std::shared_ptr<bs::sync::Wallet>&
      , const std::string& scanId, const BlockSettle::Common::ArmoryMessage_AddressTxCountResponse&);
   void registerWallet(const std::shared_ptr<bs::sync::hd::Wallet> &);
   void processWalletRegistered(const std::string &walletId);

   void processUnconfTgtSet(const std::string &walletId);
   void sendBalanceRequest(const std::string &walletId);
   void sendTxCountRequest(const std::string &walletId);
   void processAddrTxCount(const BlockSettle::Common::ArmoryMessage_AddressTxCountResponse &);
   void processWalletBal(const BlockSettle::Common::ArmoryMessage_WalletBalanceResponse &);
   void sendTrackAddrRequest(const std::string &walletId);

   void processZCReceived(const BlockSettle::Common::ArmoryMessage_ZCReceived &);

   bool processHdWalletGet(const bs::message::Envelope &, const std::string &walletId);
   bool processWalletGet(const bs::message::Envelope &, const std::string &walletId);
   bool processWalletsList(const bs::message::Envelope&
      , const BlockSettle::Common::WalletsMessage_WalletsListRequest&);
   bool processGetTxComment(const bs::message::Envelope &
      , const std::string &txBinHash);
   bool processGetWalletBalances(const bs::message::Envelope &
      , const std::string &walletId);
   bool processGetExtAddresses(const bs::message::Envelope &, const std::string &walletId);
   bool processGetIntAddresses(const bs::message::Envelope &, const std::string &walletId);
   bool processGetUsedAddresses(const bs::message::Envelope &, const std::string &walletId);
   bool sendAddresses(const bs::message::Envelope &, const std::string &walletId
      , const std::vector<bs::sync::Address> &);
   bool processCreateExtAddress(const bs::message::Envelope&, const std::string& walletId);
   bool processGetAddrComments(const bs::message::Envelope &
      , const BlockSettle::Common::WalletsMessage_WalletAddresses &);
   bool processSetAddrComments(const bs::message::Envelope &
      , const BlockSettle::Common::WalletsMessage_AddressComments &);
   bool processSetTxComment(const BlockSettle::Common::WalletsMessage_TXComment&);
   bool processTXDetails(const bs::message::Envelope &
      , const BlockSettle::Common::WalletsMessage_TXDetailsRequest &);
   bool processGetUTXOs(const bs::message::Envelope&
      , const BlockSettle::Common::WalletsMessage_UtxoListRequest&);

   void processTransactions(uint64_t msgId
      , const BlockSettle::Common::ArmoryMessage_Transactions &);
   bs::sync::Transaction::Direction getDirection(const BinaryData &txHash
      , const std::shared_ptr<bs::sync::Wallet> &, const std::map<BinaryData, Tx> &) const;
   bool processUTXOs(uint64_t msgId, const BlockSettle::Common::ArmoryMessage_UTXOs&);

   bool processSetUserId(const std::string&);
   bool processAuthKey(const bs::message::Envelope&, const std::string& address);

   bool processReserveUTXOs(const bs::message::Envelope&
      , const BlockSettle::Common::WalletsMessage_ReserveUTXOs&);
   bool processGetReservedUTXOs(const bs::message::Envelope&
      , const BlockSettle::Common::WalletsMessage_ReservationKey&);
   bool processUnreserveUTXOs(const BlockSettle::Common::WalletsMessage_ReservationKey&);

   bool processPayin(const bs::message::Envelope&
      , const BlockSettle::Common::WalletsMessage_PayinRequest&);
   bool processPayout(const bs::message::Envelope&
      , const BlockSettle::Common::WalletsMessage_PayoutRequest&);

private:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<bs::message::User>     ownUser_, blockchainUser_;
   std::unique_ptr<SignerClient>          signerClient_;
   std::shared_ptr<bs::UtxoReservation>   utxoResMgr_;

   std::mutex  mtx_;
   BinaryData  userId_;
   uint32_t    topBlock_{ 0 };
   float       settlementFee_{ 0 };
   std::vector<std::shared_ptr<bs::sync::hd::Wallet>> hdWallets_, prevHdWallets_;
   std::unordered_map<std::string, std::shared_ptr<bs::sync::Wallet>>   wallets_;
   std::unordered_map<std::string, std::unordered_set<std::string>>  pendingRegistrations_;
   std::unordered_set<std::string>     walletNames_;
   std::unordered_set<std::string>     readyWallets_;
   std::unordered_set<std::string>     loadingWallets_;
   std::shared_ptr<bs::sync::Wallet>   authAddressWallet_;
   mutable std::unordered_map<std::string, std::shared_ptr<bs::sync::hd::Group>> groupsByWalletId_;

   class CCResolver : public bs::sync::CCDataResolver
   {
   public:
      std::string nameByWalletIndex(bs::hd::Path::Elem) const override;
      uint64_t lotSizeFor(const std::string &cc) const override;
      bs::Address genesisAddrFor(const std::string &cc) const override;
      std::vector<std::string> securities() const override;

      void addData(const std::string &cc, uint64_t lotSize, const bs::Address &genAddr);

   private:
      struct CCInfo {
         uint64_t    lotSize;
         bs::Address genesisAddr;
      };
      std::unordered_map<std::string, CCInfo>   securities_;
      std::unordered_map<bs::hd::Path::Elem, std::string>   walletIdxMap_;
   };
   std::shared_ptr<CCResolver>   ccResolver_;

   struct BalanceData {
      struct BalanceBreakdown {
         BTCNumericTypes::satoshi_type spendableBalance{ 0 };
         BTCNumericTypes::satoshi_type unconfirmedBalance{ 0 };
         BTCNumericTypes::satoshi_type totalBalance{ 0 };
      };
      BalanceBreakdown  walletBalance;
      size_t   addrCount = 0;
      std::map<BinaryData, BalanceBreakdown> addressBalanceMap;
      std::map<BinaryData, uint64_t>         addressTxCntMap;
      bool  addrBalanceUpdated{ false };
      bool  addrTxCntUpdated{ false };
   };
   std::unordered_map<std::string, BalanceData> walletBalances_;

   std::unordered_map<std::string, std::set<BinaryData>> activeScanAddrs_;

   struct TXDetailData {
      bs::message::Envelope      env;
      std::map<BinaryData, Tx>   allTXs;
      std::vector<bs::sync::TXWallet>  requests;
   };
   std::map<uint64_t, TXDetailData> initialHashes_;
   std::map<uint64_t, TXDetailData> prevHashes_;

   struct UTXORequest {
      bs::message::Envelope            env;
      std::string    id;
      std::string    walletId;
      std::unordered_set<std::string>  walletIds;
      std::unordered_map<std::string, std::vector<UTXO>> spendableUTXOs;
      std::unordered_map<std::string, std::vector<UTXO>> zcUTXOs;
      bool requireZC{ false };
   };
   std::map<uint64_t, std::shared_ptr<UTXORequest>>   utxoSpendableReqs_;
   std::map<uint64_t, std::shared_ptr<UTXORequest>>   utxoZcReqs_;
   std::map<uint64_t, std::function<void(const std::vector<UTXO>&)>>    utxoReserveReqs_;
   std::map<uint64_t, std::function<void(const std::vector<Tx>& txs)>>  payinTXsCbMap_;
};


#endif	// WALLETS_ADAPTER_H
