/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_DATA_CONNECTION_H
#define WS_DATA_CONNECTION_H

#include "DataConnection.h"
#include "WsConnection.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace spdlog {
   class logger;
}
namespace bs {
   namespace network {
      class TransportClient;
   }
}

struct lws_context;

/*struct WsDataConnectionParams
{
   const void *caBundlePtr{};
   uint32_t caBundleSize{};
   bool useSsl{true};
};*/

class WsDataConnection : public DataConnection
{
public:
   WsDataConnection(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<bs::network::TransportClient> &);
   ~WsDataConnection() override;

   WsDataConnection(const WsDataConnection&) = delete;
   WsDataConnection& operator = (const WsDataConnection&) = delete;
   WsDataConnection(WsDataConnection&&) = delete;
   WsDataConnection& operator = (WsDataConnection&&) = delete;

   bool openConnection(const std::string& host, const std::string& port
      , DataConnectionListener* listener) override;
   bool closeConnection() override;

   bool send(const std::string& data) override;
   bool isActive() const override { return active_; }

   static int callbackHelper(struct lws *wsi, int reason, void *user, void *in, size_t len);

private:
   int callback(struct lws *wsi, int reason, void *user, void *in, size_t len);

   void listenFunction();

   bool sendRawData(const std::string &);
   void onRawDataReceived(const std::string& rawData) override;

   void reportFatalError(DataConnectionListener::DataConnectionError error);

   std::shared_ptr<spdlog::logger> logger_;
//   const WsDataConnectionParams params_;
   std::shared_ptr<bs::network::TransportClient>   transport_;

   lws_context *context_{};
   std::string host_;
   int port_{};
   std::atomic_bool stopped_{};
   bool  active_{ false };

   std::thread listenThread_;

   std::recursive_mutex mutex_;
   std::queue<bs::network::WsPacket> newPackets_;

   // Fields accessible from listener thread only!
   std::queue<bs::network::WsPacket> allPackets_;
   std::string currFragment_;
   lws *wsi_{};

};


#endif // WS_DATA_CONNECTION_H
