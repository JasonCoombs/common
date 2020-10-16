/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __ASSET__MANAGER_H__
#define __ASSET__MANAGER_H__

#include "CommonTypes.h"

#include <memory>
#include <unordered_map>

#include <QDateTime>
#include <QMutex>
#include <QObject>

namespace spdlog {
   class logger;
}
namespace bs {
   namespace sync {
      class Wallet;
      class WalletsManager;
   }
}
class MDCallbacksQt;
class CelerClientQt;

struct AssetCallbackTarget
{
   virtual void onCcPriceChanged(const std::string& currency) {}
   virtual void onXbtPriceChanged(const std::string& currency) {}

   virtual void onFxBalanceLoaded() {}
   virtual void onFxBalanceCleared() {}

   virtual void onBalanceChanged(const std::string& currency, double value) {}
   virtual void onTotalChanged() {}
   virtual void onSecuritiesChanged() {}
};

class AssetManager : public QObject, public AssetCallbackTarget
{
    Q_OBJECT

public:
   [[deprecated]] AssetManager(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::sync::WalletsManager> &
      , const std::shared_ptr<MDCallbacksQt> &
      , const std::shared_ptr<CelerClientQt> &);
   AssetManager(const std::shared_ptr<spdlog::logger>&, AssetCallbackTarget *);
   ~AssetManager() = default;

   virtual void init();

public:
   std::vector<std::string> currencies();
   virtual std::vector<std::string> privateShares(bool forceExternal = false);
   virtual double getBalance(const std::string& currency, const std::shared_ptr<bs::sync::Wallet> &wallet = nullptr) const;
   bool checkBalance(const std::string &currency, double amount) const;
   double getPrice(const std::string& currency) const;
   double getTotalAssets();
   double getCashTotal();
   double getCCTotal();
   uint64_t getCCLotSize(const std::string &cc) const;
   bs::Address getCCGenesisAddr(const std::string &cc) const;

   bool hasSecurities() const { return securitiesReceived_; }
   std::vector<QString> securities(bs::network::Asset::Type = bs::network::Asset::Undefined) const;

   bs::network::Asset::Type GetAssetTypeForSecurity(const std::string &security) const;

   bool HaveAssignedAccount() const { return !assignedAccount_.empty(); }
   std::string GetAssignedAccount() const { return assignedAccount_; }

signals:
   void ccPriceChanged(const std::string& currency);
   void xbtPriceChanged(const std::string& currency);

   void fxBalanceLoaded();
   void fxBalanceCleared();

   void balanceChanged(const std::string& currency);

   void totalChanged();
   void securitiesChanged();

 public slots:
    void onCCSecurityReceived(bs::network::CCSecurityDef);
    void onMDUpdate(bs::network::Asset::Type, const QString &security, bs::network::MDFields);
    void onMDSecurityReceived(const std::string &security, const bs::network::SecurityDef &sd);
    void onMDSecuritiesReceived();
    void onAccountBalanceLoaded(const std::string& currency, double value);

   void onCelerConnected();
   void onCelerDisconnected();
   void onWalletChanged();

protected:
   bool securityDef(const std::string &security, bs::network::SecurityDef &) const;

private:
  void sendUpdatesOnXBTPrice(const std::string& ccy);

  void onCcPriceChanged(const std::string& currency) override { emit ccPriceChanged(currency); }
  void onXbtPriceChanged(const std::string& currency) override { emit xbtPriceChanged(currency); }
  void onFxBalanceLoaded() override { emit fxBalanceLoaded(); }
  void onFxBalanceCleared() override { emit fxBalanceCleared(); }

  void onBalanceChanged(const std::string& currency, double) override { emit balanceChanged(currency); }
  void onTotalChanged() override { emit totalChanged(); }
  void onSecuritiesChanged() override { emit securitiesChanged(); }

protected:
   std::shared_ptr<spdlog::logger>        logger_;
   std::shared_ptr<bs::sync::WalletsManager> walletsManager_;
   std::shared_ptr<MDCallbacksQt>         mdCallbacks_;
   std::shared_ptr<CelerClientQt>         celerClient_;
   AssetCallbackTarget* act_{ nullptr };

   bool     securitiesReceived_ = false;
   std::vector<std::string>   currencies_;
   QMutex   mtxCurrencies_;
   std::unordered_map<std::string, double> balances_;
   std::unordered_map<std::string, double> prices_;
   std::unordered_map<std::string, bs::network::SecurityDef>   securities_;
   std::unordered_map<std::string, bs::network::CCSecurityDef> ccSecurities_;

   std::string assignedAccount_;

   std::unordered_map<std::string, QDateTime>  xbtPriceUpdateTimes_;
};

#endif // __ASSET__MANAGER_H__
