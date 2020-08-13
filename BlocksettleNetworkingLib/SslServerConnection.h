/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef SSL_SERVER_CONNECTION_H
#define SSL_SERVER_CONNECTION_H

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

struct SslServerConnectionParams
{
   // If set, client's IP address will be read from x-forwarded-for header value if possible.
   // Last IP address in a list will be used - https://en.wikipedia.org/wiki/X-Forwarded-For#Format
   bool trustForwardedForHeader{};
};

class SslServerConnection : public ServerConnection
{
public:
   SslServerConnection(const std::shared_ptr<spdlog::logger>& logger, SslServerConnectionParams params);
   ~SslServerConnection() override;

   SslServerConnection(const SslServerConnection&) = delete;
   SslServerConnection& operator = (const SslServerConnection&) = delete;
   SslServerConnection(SslServerConnection&&) = delete;
   SslServerConnection& operator = (SslServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   bool SendDataToClient(const std::string& clientId, const std::string& data) override;
   bool SendDataToAllClients(const std::string&) override;

   static int callbackHelper(struct lws *wsi, int reason, void *in, size_t len);

private:
   struct WsServerDataToSend
   {
      std::string clientId;
      bs::network::WsRawPacket packet;
   };

   struct WsServerClientData
   {
      std::string clientId;
      lws *wsi{};
      std::queue<bs::network::WsRawPacket> packets;
      std::string currFragment;
   };

   std::thread::id listenThreadId() const;

   void listenFunction();

   void stopServer();

   int callback(struct lws *wsi, int reason, void *in, size_t len);

   std::string nextClientId();

   std::shared_ptr<spdlog::logger>  logger_;
   const SslServerConnectionParams params_;

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

#endif // SSL_SERVER_CONNECTION_H
