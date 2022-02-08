/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CC_FILE_MANAGER_H__
#define __CC_FILE_MANAGER_H__

#include <functional>
#include <memory>
#include <vector>

#include <QString>
#include <QVariant>
#include <QPointer>
#include "Address.h"
#include "CommonTypes.h"
#include "Wallets/SyncWallet.h"

class ApplicationSettings;

struct CCCallbackTarget
{
   virtual void onCCSecurityDef(const bs::network::CCSecurityDef&) {}
   virtual void onCCSecurityId(const std::string& securityId) {}
   virtual void onCCSecurityInfo(const std::string& cc, unsigned long nbSatoshis
      , const bs::Address& genesisAddr) {}

   virtual void onCCAddressSubmitted(const bs::Address&) {}
   virtual void onCCInitialSubmitted(const bs::Address&) {}
   virtual void onCCSubmitFailed(const bs::Address& address, const std::string& err) {}
   virtual void onLoaded() {}
};

class CCPubResolver : public bs::sync::CCDataResolver
{
public:

   using CCSecLoadedCb = std::function<void(const bs::network::CCSecurityDef &)>;
   using CCLoadCompleteCb = std::function<void()>;
   CCPubResolver(const std::shared_ptr<spdlog::logger> &logger
      , const std::string &signAddress, const CCSecLoadedCb &cbSec
      , const CCLoadCompleteCb &cbLoad)
      : logger_(logger), signAddress_(signAddress), cbSecLoaded_(cbSec)
      , cbLoadComplete_(cbLoad) {}

   std::string nameByWalletIndex(bs::hd::Path::Elem) const override;
   uint64_t lotSizeFor(const std::string &cc) const override;
   bs::Address genesisAddrFor(const std::string &cc) const override;
   std::vector<std::string> securities() const override;

   void fillFrom(const std::vector<bs::network::CCSecurityDef>& definitions);

private:
   void add(const bs::network::CCSecurityDef &);
   void clear();

private:
   std::shared_ptr<spdlog::logger>  logger_;
   const std::string                signAddress_;
   std::map<std::string, bs::network::CCSecurityDef>  securities_;
   std::map<bs::hd::Path::Elem, std::string>          walletIdxMap_;
   const CCSecLoadedCb     cbSecLoaded_;
   const CCLoadCompleteCb  cbLoadComplete_;
};

class CCFileManager : public QObject, public CCCallbackTarget
{
Q_OBJECT
public:
   [[deprecated]] CCFileManager(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<ApplicationSettings> &appSettings);
   CCFileManager(const std::shared_ptr<spdlog::logger>&, CCCallbackTarget*
      , const std::string& signAddress);
   ~CCFileManager() noexcept override = default;

   CCFileManager(const CCFileManager&) = delete;
   CCFileManager& operator = (const CCFileManager&) = delete;
   CCFileManager(CCFileManager&&) = delete;
   CCFileManager& operator = (CCFileManager&&) = delete;

   std::shared_ptr<bs::sync::CCDataResolver> getResolver() const { return resolver_; }

   bool submitAddress(const bs::Address &, uint32_t seed, const std::string &ccProduct);
   bool wasAddressSubmitted(const bs::Address &);
   void cancelActiveSign();

   void SetLoadedDefinitions(const std::vector<bs::network::CCSecurityDef>& definitions);

signals:
   void CCSecurityDef(bs::network::CCSecurityDef);
   void CCSecurityId(const std::string& securityId);
   void CCSecurityInfo(QString ccProd, unsigned long nbSatoshis, QString genesisAddr);

   void CCAddressSubmitted(const QString);
   void CCInitialSubmitted(const QString);
   void CCSubmitFailed(const QString address, const QString &err);
   void Loaded();

private: // Callbacks override
   void onCCSecurityDef(const bs::network::CCSecurityDef& sd) override { emit CCSecurityDef(sd); }
   void onCCSecurityId(const std::string& securityId) override { emit CCSecurityId(securityId); }
   void onCCSecurityInfo(const std::string& cc, unsigned long nbSatoshis
      , const bs::Address& genesisAddr) {
      emit CCSecurityInfo(QString::fromStdString(cc), nbSatoshis, QString::fromStdString(genesisAddr.display()));
   }

   void onCCAddressSubmitted(const bs::Address& addr) override { emit CCAddressSubmitted(QString::fromStdString(addr.display())); }
   void onCCInitialSubmitted(const bs::Address& addr) override { emit CCInitialSubmitted(QString::fromStdString(addr.display())); }
   void onCCSubmitFailed(const bs::Address& addr, const std::string& err) override {
      emit CCSubmitFailed(QString::fromStdString(addr.display()), QString::fromStdString(err));
   }
   void onLoaded() override { emit Loaded(); }

private:
   std::shared_ptr<spdlog::logger>        logger_;
   CCCallbackTarget* cct_{ nullptr };

   std::shared_ptr<CCPubResolver>         resolver_;
};

#endif // __CC_FILE_MANAGER_H__
