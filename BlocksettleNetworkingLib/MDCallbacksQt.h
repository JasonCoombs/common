/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __MD_CALLBACKS_QT_H__
#define __MD_CALLBACKS_QT_H__

#include <QObject>
#include <memory>
#include <string>
#include <unordered_map>

#include "MarketDataProvider.h"


namespace spdlog
{
   class logger;
}

class MDCallbacksQt : public QObject, public MDCallbackTarget
{
Q_OBJECT
public:
   MDCallbacksQt() : QObject() {}
   ~MDCallbacksQt() noexcept override = default;

   void userWantsToConnect() override
   {
      emit UserWantToConnectToMD();
   }

   void waitingForConnectionDetails() override
   {
      emit WaitingForConnectionDetails();
   }

   void startConnecting() override
   {
      emit StartConnecting();
   }

   void connected() override
   {
      emit Connected();
   }

   void disconnecting() override
   {
      emit Disconnecting();
   }

   void disconnected() override
   {
      emit Disconnected();
   }

   void onRequestRejected(const std::string &security
      , const std::string &reason) override
   {
      emit MDReqRejected(security, reason);
   }

   void onMDUpdate(bs::network::Asset::Type, const std::string &security
      , bs::network::MDFields) override;
   void onMDSecurityReceived(const std::string &security
      , const bs::network::SecurityDef &) override;

   void allSecuritiesReceived() override
   {
      emit MDSecuritiesReceived();
   }

   void onNewFXTrade(const bs::network::NewTrade &trade) override
   {
      emit OnNewFXTrade(trade);
   }

   void onNewXBTTrade(const bs::network::NewTrade &trade) override
   {
      emit OnNewXBTTrade(trade);
   }

   void onNewPMTrade(const bs::network::NewPMTrade &trade) override
   {
      emit OnNewPMTrade(trade);
   }

signals:
   void UserWantToConnectToMD();

   void WaitingForConnectionDetails();

   void StartConnecting();
   void Connected();

   void Disconnecting();
   void Disconnected();

   void MDUpdate(bs::network::Asset::Type, const QString &security, bs::network::MDFields);
   void MDSecurityReceived(const std::string &security, const bs::network::SecurityDef &sd);
   void MDSecuritiesReceived();
   void MDReqRejected(const std::string &security, const std::string &reason);

   void OnNewFXTrade(const bs::network::NewTrade& trade);
   void OnNewXBTTrade(const bs::network::NewTrade& trade);
   void OnNewPMTrade(const bs::network::NewPMTrade& trade);
};

#endif // __MD_CALLBACKS_QT_H__
