/*

***********************************************************************************
* Copyright (C) 2018 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __MARKET_DATA_PROVIDER_H__
#define __MARKET_DATA_PROVIDER_H__

#include <memory>
#include <string>
#include <unordered_map>
#include "CommonTypes.h"


namespace spdlog {
   class logger;
}

class MDCallbackTarget
{
public:
   virtual ~MDCallbackTarget() = default;
   virtual void userWantsToConnect() {}
   virtual void waitingForConnectionDetails() {}
   virtual void startConnecting() {}
   virtual void connected() {}
   virtual void disconnecting() {}
   virtual void disconnected() {}
   // arguments: security, reason
   virtual void onRequestRejected(const std::string &
      , const std::string &) {}

   virtual void onMDUpdate(bs::network::Asset::Type, const std::string &
      , bs::network::MDFields) {}
   virtual void onMDSecurityReceived(const std::string &
      , const bs::network::SecurityDef &) {}
   virtual void allSecuritiesReceived() {}

   virtual void onNewFXTrade(const bs::network::NewTrade &) {}
   virtual void onNewXBTTrade(const bs::network::NewTrade &) {}
   virtual void onNewPMTrade(const bs::network::NewPMTrade &) {}
};

class MarketDataProvider
{
public:
   MarketDataProvider(const std::shared_ptr<spdlog::logger> &
      , MDCallbackTarget *);
   virtual ~MarketDataProvider() noexcept = default;

   MarketDataProvider(const MarketDataProvider&) = delete;
   MarketDataProvider& operator = (const MarketDataProvider&) = delete;

   MarketDataProvider(MarketDataProvider&&) = delete;
   MarketDataProvider& operator = (MarketDataProvider&&) = delete;

   void SetConnectionSettings(const std::string &host, const std::string &port);

   void SubscribeToMD();
   void UnsubscribeFromMD();
   virtual void MDLicenseAccepted();

   virtual bool DisconnectFromMDSource() { return true; }
   virtual bool IsConnectionActive() const { return false; }

protected:
   virtual bool StartMDConnection() { return true; }
   virtual void StopMDConnection() { }

protected:
   std::shared_ptr<spdlog::logger>  logger_;
   MDCallbackTarget              *  callbacks_;

   bool waitingForConnectionDetails_ = false;

   std::string host_;
   std::string port_;
};

#endif // __MARKET_DATA_PROVIDER_H__
