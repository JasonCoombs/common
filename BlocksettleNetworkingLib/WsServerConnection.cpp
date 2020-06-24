/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WsServerConnection.h"

#include "StringUtils.h"
#include "ThreadName.h"
#include "Transport.h"
#include "WsConnection.h"

#include <random>
#include <libwebsockets.h>
#include <spdlog/spdlog.h>

using namespace bs::network;

namespace {

   const auto kPeriodicCheckTimeout = std::chrono::seconds(30);

   int callback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
   {
      return WsServerConnection::callbackHelper(wsi, reason, in, len);
   }
   struct lws_protocols kProtocols[] = {
   { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

} // namespace

WsServerConnection::WsServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<bs::network::TransportServer> &tr)
   : logger_(logger)
   , transport_(tr)
{
   transport_->setClientErrorCb(
      [this](const std::string &id, const std::string &err) { listener_->onClientError(id, err); }
      , [this](const std::string &clientId, ServerConnectionListener::ClientError errorCode
         , int socket) { listener_->onClientError(clientId, errorCode, socket); });
   transport_->setDataReceivedCb([this](const std::string &clientId, const std::string &data) {
      listener_->OnDataFromClient(clientId, data); });
   transport_->setSendDataCb([this](const std::string &clientId, const std::string &data) {
      return sendData(clientId, data); });
   transport_->setConnectedCb([this](const std::string &clientId) { listener_->OnClientConnected(clientId); }
      , [this](const std::string &clientId) { listener_->OnClientDisconnected(clientId); });
}

WsServerConnection::~WsServerConnection()
{
   stopServer();
}

bool WsServerConnection::BindConnection(const std::string& host , const std::string& port
   , ServerConnectionListener* listener)
{
   stopServer();

   std::random_device rd;
   std::mt19937_64 gen(rd());
   std::uniform_int_distribution<uint64_t> dis;
   nextClientId_ = dis(gen);

   struct lws_context_creation_info info;
   memset(&info, 0, sizeof(info));
   info.port = std::stoi(port);
   info.protocols = kProtocols;
   info.ws_ping_pong_interval = kPingPongInterval / std::chrono::seconds(1);
   info.gid = -1;
   info.uid = -1;
   info.options = LWS_SERVER_OPTION_VALIDATE_UTF8 | LWS_SERVER_OPTION_DISABLE_IPV6;
   info.user = this;

   context_ = lws_create_context(&info);
   if (context_ == nullptr) {
      SPDLOG_LOGGER_ERROR(logger_, "context create failed");
      return false;
   }

   stopped_ = false;
   listener_ = listener;

   listenThread_ = std::thread(&WsServerConnection::listenFunction, this);

   return true;
}

void WsServerConnection::listenFunction()
{
   bs::setCurrentThreadName("WsServer");

   while (!stopped_.load()) {
      lws_service(context_, kPeriodicCheckTimeout / std::chrono::milliseconds(1));
      transport_->periodicCheck();
   }
}

void WsServerConnection::stopServer()
{
   if (!listenThread_.joinable()) {
      return;
   }

   stopped_ = true;
   lws_cancel_service(context_);

   listenThread_.join();

   lws_context_destroy(context_);
   listener_ = nullptr;
   context_ = nullptr;
   packets_ = {};
   clients_ = {};
   socketToClientIdMap_ = {};
}

int WsServerConnection::callback(lws *wsi, int reason, void *in, size_t len)
{
   switch (reason) {
      case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
         std::queue<WsServerDataToSend> packets;
         {  std::lock_guard<std::recursive_mutex> lock(mutex_);
            std::swap(packets, packets_);
         }
         while (!packets.empty()) {
            auto data = std::move(packets.front());
            packets.pop();

            auto clientIt = clients_.find(data.clientId);
            if (clientIt == clients_.end()) {
               SPDLOG_LOGGER_DEBUG(logger_, "send failed, client {} already disconnected"
                  , bs::toHex(data.clientId));
               continue;
            }
            auto &client = clientIt->second;
            client.packets.push(std::move(data.packet));
            lws_callback_on_writable(client.wsi);
         }
         break;
      }

      case LWS_CALLBACK_ESTABLISHED: {
         auto clientId = nextClientId();
         auto &client = clients_[clientId];
         client.wsi = wsi;
         client.clientId = clientId;

         socketToClientIdMap_[wsi] = clientId;
         SPDLOG_LOGGER_DEBUG(logger_, "new client connected: {}", bs::toHex(clientId));
         break;
      }

      case LWS_CALLBACK_CLOSED: {
         auto clientId = socketToClientIdMap_.at(wsi);
         SPDLOG_LOGGER_DEBUG(logger_, "client disconnected: {}", bs::toHex(clientId));
         socketToClientIdMap_.erase(wsi);
         size_t count = clients_.erase(clientId);
         assert(count == 1);
         break;
      }

      case LWS_CALLBACK_RECEIVE: {
         auto clientId = socketToClientIdMap_.at(wsi);
         auto &client = clients_.at(clientId);
         auto ptr = static_cast<const char*>(in);
         client.currFragment.insert(client.currFragment.end(), ptr, ptr + len);
         if (lws_remaining_packet_payload(wsi) > 0) {
            return 0;
         }
         if (!lws_is_final_fragment(wsi)) {
            SPDLOG_LOGGER_ERROR(logger_, "unexpected fragment");
            return -1;
         }
         transport_->processIncomingData(client.currFragment, clientId, -1);
         client.currFragment.clear();
         break;
      }

      case LWS_CALLBACK_SERVER_WRITEABLE: {
         auto clientId = socketToClientIdMap_.at(wsi);
         auto &client = clients_.at(clientId);
         if (client.packets.empty()) {
            return 0;
         }

         auto packet = std::move(client.packets.front());
         client.packets.pop();

         int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
         if (rc == -1) {
            SPDLOG_LOGGER_ERROR(logger_, "write failed");
            return -1;
         }
         if (rc != static_cast<int>(packet.getSize())) {
             SPDLOG_LOGGER_ERROR(logger_, "write truncated");
             return -1;
         }

         if (!client.packets.empty()) {
            lws_callback_on_writable(client.wsi);
         }

         break;
      }
   }

   return 0;
}

std::string WsServerConnection::nextClientId()
{
   nextClientId_ += 1;
   auto ptr = reinterpret_cast<char*>(&nextClientId_);
   return std::string(ptr, ptr + sizeof(nextClientId_));
}

std::thread::id WsServerConnection::listenThreadId() const
{
   return listenThread_.get_id();
}

std::string WsServerConnection::GetClientInfo(const std::string &clientId) const
{
   return {};
}

bool WsServerConnection::SendDataToClient(const std::string &clientId, const std::string &data)
{
   return transport_->sendData(clientId, data);
}

bool WsServerConnection::sendData(const std::string &clientId, const std::string &data)
{
   WsServerDataToSend toSend{ clientId, WsPacket(data) };
   {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      packets_.push(std::move(toSend));
   }
   lws_cancel_service(context_);
   return true;
}

bool WsServerConnection::SendDataToAllClients(const std::string &data)
{  // In encrypted environments data can't be broadcasted - it needs to be first
   // encrypted by per-connection keys (which are different for each client).
   // So we use multicast here.
   size_t cntClients = clients_.size();
   for (const auto &item : clients_) {
      if (SendDataToClient(item.first, data)) {
         cntClients--;
      }
   }
   return (cntClients == 0);
}

int WsServerConnection::callbackHelper(lws *wsi, int reason, void *in, size_t len)
{
   auto context = lws_get_context(wsi);
   auto server = static_cast<WsServerConnection*>(lws_context_user(context));
   return server->callback(wsi, reason, in, len);
}
