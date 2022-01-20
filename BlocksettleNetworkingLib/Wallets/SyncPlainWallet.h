/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef BS_SYNC_PLAIN_WALLET_H
#define BS_SYNC_PLAIN_WALLET_H

#include <memory>
#include <unordered_map>
#include <QObject>
#include <BinaryData.h>

#include "SyncWallet.h"


namespace spdlog {
   class logger;
};

class WalletSignerContainer;

namespace bs {
   namespace sync {

      // A base wallet that can be used by other wallets, or for very basic
      // functionality (e.g., creating a bare wallet that can be registered and get
      // info on addresses added to the wallet). The wallet may or may not be able
      // to access the wallet DB.
      class PlainWallet : public Wallet
      {
      public:
         PlainWallet(const std::string &walletId, const std::string &name, const std::string &desc
            , WalletSignerContainer *, const std::shared_ptr<spdlog::logger> &logger);
         ~PlainWallet() override;

         PlainWallet(const PlainWallet&) = delete;
         PlainWallet(PlainWallet&&) = delete;
         PlainWallet& operator = (const PlainWallet&) = delete;
         PlainWallet& operator = (PlainWallet&&) = delete;

         int addAddress(const bs::Address &, const std::string &index
            , bool sync = true) override;
         bool containsAddress(const bs::Address &addr) override;

         std::string walletId() const override { return walletId_; }
         std::string description() const override { return desc_; }
         void setDescription(const std::string &desc) override { desc_ = desc; }
         bs::core::wallet::Type type() const override { return bs::core::wallet::Type::Bitcoin; }

         void getNewExtAddress(const CbAddress &) override;
         void getNewIntAddress(const CbAddress &cb) override { getNewExtAddress(cb); }
         size_t getUsedAddressCount() const override { return usedAddresses_.size(); }
         std::string getAddressIndex(const bs::Address &) override;

         std::shared_ptr<Armory::Signer::ResolverFeed> getPublicResolver() const override { return nullptr; }   // not needed, yet

         bool deleteRemotely() override;

         void merge(const std::shared_ptr<Wallet> &) override
         {
            throw std::runtime_error("not implemented yet. not sure is necessary");
         }

      protected:
         std::vector<BinaryData> getAddrHashes() const override;

      protected:
         mutable std::set<BinaryData>  addrPrefixedHashes_;

      private:
         int addressIndex(const bs::Address &) const;

      private:
         std::string    walletId_;
         std::string    desc_;
      };

   }  //namespace sync
}  //namespace bs

#endif // BS_SYNC_PLAIN_WALLET_H
