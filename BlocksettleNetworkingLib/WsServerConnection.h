/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_SERVER_CONNECTION_H
#define WS_SERVER_CONNECTION_H

#include "ServerConnection.h"
#include "WsConnection.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <map>

namespace spdlog {
   class logger;
}

struct lws_context;
struct lws;

struct WsServerConnectionParams
{
};

class WsServerConnection : public ServerConnection
{
public:
   WsServerConnection(const std::shared_ptr<spdlog::logger>& logger, WsServerConnectionParams params);
   ~WsServerConnection() override;

   WsServerConnection(const WsServerConnection&) = delete;
   WsServerConnection& operator = (const WsServerConnection&) = delete;
   WsServerConnection(WsServerConnection&&) = delete;
   WsServerConnection& operator = (WsServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   std::string GetClientInfo(const std::string &clientId) const override;

   bool SendDataToClient(const std::string& clientId, const std::string& data) override;
   bool SendDataToAllClients(const std::string&) override;

   static int callbackHelper(struct lws *wsi, int reason, void *in, size_t len);

private:
   struct WsServerDataToSend
   {
      std::string clientId;
      bs::network::WsPacket packet;
   };

   struct WsServerClientData
   {
      std::string clientId;
      lws *wsi{};
      std::queue<bs::network::WsPacket> packets;
      std::string currFragment;
   };

   std::thread::id listenThreadId() const;

   void listenFunction();

   void stopServer();

   int callback(struct lws *wsi, int reason, void *in, size_t len);

   std::string nextClientId();

   std::shared_ptr<spdlog::logger>  logger_;
   const WsServerConnectionParams params_;

   std::thread listenThread_;
   ServerConnectionListener *listener_{};
   std::atomic_bool stopped_{};
   lws_context *context_{};

   std::recursive_mutex mutex_;
   std::queue<WsServerDataToSend> packets_;

   // Fields accessible from listener thread only
   std::map<std::string, WsServerClientData> clients_;
   std::map<lws*, std::string> socketToClientIdMap_;
   uint64_t nextClientId_{};

};

#endif // WS_SERVER_CONNECTION_H
