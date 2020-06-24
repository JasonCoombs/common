/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WsDataConnection.h"

#include <libwebsockets.h>
#include <spdlog/spdlog.h>
#include "Transport.h"
#include "WsConnection.h"


using namespace bs::network;

namespace {

   const auto kPeriodicCheckTimeout = std::chrono::seconds(120);

   int callback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
   {
      return WsDataConnection::callbackHelper(wsi, reason, user, in, len);
   }

   struct lws_protocols kProtocols[] = {
      { kProtocolNameWs, callback, 0, kRxBufferSize, kId, nullptr, kTxPacketSize },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 },
   };

} // namespace


WsDataConnection::WsDataConnection(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<bs::network::TransportClient> &tr)
   : logger_(logger)
   , transport_(tr)
{
   transport_->setSendCb([this](const std::string &d) { return sendRawData(d); });
   transport_->setNotifyDataCb([this](const std::string &d) { notifyOnData(d);});
   transport_->setSocketErrorCb([this](DataConnectionListener::DataConnectionError e) { reportFatalError(e); });
}

WsDataConnection::~WsDataConnection()
{
   closeConnection();
}

bool WsDataConnection::openConnection(const std::string &host, const std::string &port
   , DataConnectionListener *listener)
{
   closeConnection();

   listener_ = listener;
   host_ = host;
   port_ = std::stoi(port);
   stopped_ = false;

   transport_->openConnection(host, port);

   struct lws_context_creation_info info;
   memset(&info, 0, sizeof(info));

   info.port = CONTEXT_PORT_NO_LISTEN;
   info.protocols = kProtocols;
   info.gid = -1;
   info.uid = -1;
   info.ws_ping_pong_interval = kPingPongInterval / std::chrono::seconds(1);
   info.options = /*params_.useSsl ? LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT :*/ 0;
   info.user = this;

   context_ = lws_create_context(&info);
   if (!context_) {
      SPDLOG_LOGGER_ERROR(logger_, "context create failed");
      return false;
   }

   listenThread_ = std::thread(&WsDataConnection::listenFunction, this);

   return true;
}

bool WsDataConnection::closeConnection()
{
   if (!listenThread_.joinable()) {
      return false;
   }
   transport_->closeConnection();

   stopped_ = true;
   lws_cancel_service(context_);

   listenThread_.join();

   lws_context_destroy(context_);
   context_ = nullptr;
   listener_ = nullptr;
   newPackets_ = {};
   allPackets_ = {};
   currFragment_ = {};

   return true;
}

bool WsDataConnection::send(const std::string &data)
{
   return transport_->sendData(data);
}

bool WsDataConnection::sendRawData(const std::string &data)
{
   if (!context_) {
      return false;
   }
   {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      newPackets_.push(WsPacket(data));
   }
   lws_cancel_service(context_);
   return true;
}

int WsDataConnection::callbackHelper(lws *wsi, int reason, void *user, void *in, size_t len)
{
   auto context = lws_get_context(wsi);
   auto client = static_cast<WsDataConnection*>(lws_context_user(context));
   return client->callback(wsi, reason, user, in, len);
}

int WsDataConnection::callback(lws *wsi, int reason, void *user, void *in, size_t len)
{
   switch (reason) {
      case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
//         if (!params_.useSsl || params_.caBundleSize == 0) {
            return 0;
/*         }
         auto sslCtx = static_cast<SSL_CTX*>(user);
         auto store = SSL_CTX_get_cert_store(sslCtx);
         auto bio = BIO_new_mem_buf(params_.caBundlePtr, static_cast<int>(params_.caBundleSize));
         while (true) {
            auto x = PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr);
            if (!x) {
               break;
            }
            int rc = X509_STORE_add_cert(store, x);
            assert(rc);
            X509_free(x);
         }
         BIO_free(bio);
         break;*/
      }

      case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
         {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            while (!newPackets_.empty()) {
               allPackets_.push(std::move(newPackets_.front()));
               newPackets_.pop();
            }
         }
         if (!allPackets_.empty()) {
            if (wsi_) {
               lws_callback_on_writable(wsi_);
            }
         }
         break;
      }

      case LWS_CALLBACK_CLIENT_RECEIVE: {
         if (transport_->connectTimedOut()) {
            if (transport_->handshakeTimedOut()) {
               SPDLOG_LOGGER_ERROR(logger_, "BIP connection is timed out (bip151"
                  " was completed, probably client credential is not valid)");
               reportFatalError(DataConnectionListener::HandshakeFailed);
            } else {
               SPDLOG_LOGGER_ERROR(logger_, "BIP connection is timed out");
               reportFatalError(DataConnectionListener::ConnectionTimeout);
            }
            break;
         }
         transport_->triggerHeartbeatCheck();

         auto ptr = static_cast<const char*>(in);
         currFragment_.insert(currFragment_.end(), ptr, ptr + len);
         if (lws_remaining_packet_payload(wsi) > 0) {
            return 0;
         }
         if (!lws_is_final_fragment(wsi)) {
            SPDLOG_LOGGER_ERROR(logger_, "unexpected fragment");
            reportFatalError(DataConnectionListener::ProtocolViolation);
            return -1;
         }
         onRawDataReceived(currFragment_);
         currFragment_.clear();
         break;
      }

      case LWS_CALLBACK_CLIENT_WRITEABLE: {
         if (allPackets_.empty()) {
            return 0;
         }
         auto packet = std::move(allPackets_.front());
         allPackets_.pop();
         int rc = lws_write(wsi, packet.getPtr(), packet.getSize(), LWS_WRITE_BINARY);
         if (rc == -1) {
            SPDLOG_LOGGER_ERROR(logger_, "write failed");
            reportFatalError(DataConnectionListener::UndefinedSocketError);
            return -1;
         }
         if (rc != static_cast<int>(packet.getSize())) {
             SPDLOG_LOGGER_ERROR(logger_, "write truncated");
             reportFatalError(DataConnectionListener::UndefinedSocketError);
             return -1;
         }
         if (!allPackets_.empty()) {
            lws_callback_on_writable(wsi);
         }
         break;
      }

      case LWS_CALLBACK_CLIENT_ESTABLISHED: {
         active_ = true;
         transport_->socketConnected();
         transport_->startHandshake();
         break;
      }

      case LWS_CALLBACK_CLIENT_CLOSED: {
         active_ = false;
         listener_->OnDisconnected();
         transport_->socketDisconnected();
         break;
      }

      case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
         active_ = false;
         listener_->OnError(DataConnectionListener::UndefinedSocketError);
         break;
      }
   }

   return 0;
}

void WsDataConnection::listenFunction()
{
   struct lws_client_connect_info i;
   memset(&i, 0, sizeof(i));
   i.address = host_.c_str();
   i.host = i.address;
   i.port = port_;
   i.origin = i.address;
   i.path = "/";
   i.context = context_;
   i.protocol = kProtocolNameWs;
   i.userdata = this;

   i.ssl_connection = /*params_.useSsl ? LCCSCF_USE_SSL :*/ 0;

   wsi_ = lws_client_connect_via_info(&i);

   while (!stopped_.load()) {
      lws_service(context_, kPeriodicCheckTimeout / std::chrono::milliseconds(1));
   }

   wsi_ = nullptr;
}

void WsDataConnection::onRawDataReceived(const std::string &rawData)
{
   transport_->onRawDataReceived(rawData);
}

void WsDataConnection::reportFatalError(DataConnectionListener::DataConnectionError error)
{
   if (error == DataConnectionListener::NoError) {
      listener_->OnConnected();
      return;
   }
   logger_->debug("[{}] error: {}", __func__, (int)error);
   transport_->sendDisconnect();
   stopped_ = true;
   lws_cancel_service(context_);
   wsi_ = nullptr;
   listener_->OnError(error);
}
