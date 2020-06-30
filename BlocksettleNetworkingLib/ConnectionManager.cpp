/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ConnectionManager.h"

#include "CelerClientConnection.h"
#include "CelerStreamServerConnection.h"
#include "GenoaConnection.h"
#include "GenoaStreamServerConnection.h"
#include "PublisherConnection.h"
#include "SubscriberConnection.h"
#include "ZmqContext.h"
#include "ZmqDataConnection.h"
#include "TransportBIP15x.h"
#include "TransportBIP15xServer.h"

#include <QNetworkAccessManager>

#ifdef Q_OS_WIN
   #include <Winsock2.h>
#endif

#include <zmq.h>
#include <spdlog/spdlog.h>

ConnectionManager::ConnectionManager(const std::shared_ptr<spdlog::logger>& logger)
   : logger_(logger)
{
   // init network
   isInitialized_ = InitNetworkLibs();
}

ConnectionManager::ConnectionManager(const std::shared_ptr<spdlog::logger>& logger
   , const bs::network::BIP15xPeers &zmqTrustedTerminals)
   : logger_(logger), zmqTrustedTerminals_(zmqTrustedTerminals)
{
   // init network
   isInitialized_ = InitNetworkLibs();
}

ConnectionManager::ConnectionManager(const std::shared_ptr<spdlog::logger>& logger
   , std::shared_ptr<ArmoryServersProvider> armoryServers)
   : logger_(logger), armoryServers_(armoryServers)
{
   // init network
   isInitialized_ = InitNetworkLibs();
}

bool ConnectionManager::InitNetworkLibs()
{
#ifdef Q_OS_WIN
   WORD  wVersion;
   WSADATA wsaData;
   int err;

   wVersion = MAKEWORD(2,0);
   err = WSAStartup( wVersion, &wsaData );
   if (err) {
      return false;
   }
#endif

   zmqContext_ = std::make_shared<ZmqContext>(logger_);

   return true;
}

void ConnectionManager::DeinitNetworkLibs()
{
#ifdef Q_OS_WIN
   WSACleanup();
#endif
}

ConnectionManager::~ConnectionManager() noexcept
{
   DeinitNetworkLibs();
}

std::shared_ptr<spdlog::logger> ConnectionManager::GetLogger() const
{
   return logger_;
}

std::shared_ptr<ServerConnection> ConnectionManager::CreateGenoaAPIServerConnection() const
{
   return std::make_shared<GenoaStreamServerConnection>(logger_, zmqContext_);
}

std::shared_ptr<ServerConnection> ConnectionManager::CreateCelerAPIServerConnection() const
{
   return std::make_shared<CelerStreamServerConnection>(logger_, zmqContext_);
}

std::shared_ptr<DataConnection> ConnectionManager::CreateCelerClientConnection() const
{
   auto connection = std::make_shared<CelerClientConnection<ZmqDataConnection> >(logger_);
   connection->SetContext(zmqContext_);

   return connection;
}

std::shared_ptr<DataConnection> ConnectionManager::CreateGenoaClientConnection(bool monitored) const
{
   auto connection = std::make_shared<GenoaConnection<ZmqDataConnection> >(logger_, monitored);
   connection->SetContext(zmqContext_);

   return connection;
}

std::shared_ptr<ServerConnection> ConnectionManager::createZmqBIP15xChatServerConnection(
   bool ephemeral, const std::string& ownKeyFileDir, const std::string& ownKeyFileName) const
{
   auto cbTrustedClients = [this]() {
      return zmqTrustedTerminals_;
   };
   const auto &bip15xTransport = std::make_shared<bs::network::TransportBIP15xServer>(
      logger_, cbTrustedClients, ephemeral, ownKeyFileDir, ownKeyFileName, false);

   return std::make_shared<GenoaStreamServerConnection>(logger_, zmqContext_
      , bip15xTransport);
}

std::unique_ptr<DataConnection> ConnectionManager::createZmqBIP15xDataConnection(
   const std::shared_ptr<bs::network::TransportBIP15xClient> &transport) const
{
   auto conn = std::make_unique<ZmqBinaryConnection>(logger_, transport);
   conn->SetContext(zmqContext_);
   return conn;
}

std::shared_ptr<DataConnection> ConnectionManager::createZmqBIP15xDataConnection() const
{
   bs::network::BIP15xParams params;
   params.ephemeralPeers = true;
   const auto &transport = std::make_shared<bs::network::TransportBIP15xClient>(logger_, params);
   auto conn = std::make_shared<ZmqBinaryConnection>(logger_, transport);
   conn->SetContext(zmqContext_);
   return conn;
}

std::shared_ptr<ServerConnection> ConnectionManager::CreatePubBridgeServerConnection() const
{
   return std::make_shared<GenoaStreamServerConnection>(logger_, zmqContext_);
}

// MD will be sent as HTTP packet
// each genoa message ( send or received ) ends with double CRLF.
std::shared_ptr<ServerConnection> ConnectionManager::CreateMDRestServerConnection() const
{
   return std::make_shared<GenoaStreamServerConnection>(logger_, zmqContext_);
}

std::shared_ptr<PublisherConnection> ConnectionManager::CreatePublisherConnection() const
{
   return std::make_shared<PublisherConnection>(logger_, zmqContext_);
}

std::shared_ptr<SubscriberConnection> ConnectionManager::CreateSubscriberConnection() const
{
   return std::make_shared<SubscriberConnection>(logger_, zmqContext_);
}

const std::shared_ptr<QNetworkAccessManager> &ConnectionManager::GetNAM()
{
   if (!nam_) {
      nam_.reset(new QNetworkAccessManager);
   }

   return nam_;
}
