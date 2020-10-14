/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "Bip15xDataConnection.h"

#include <spdlog/spdlog.h>
#include "Transport.h"
#include "BinaryData.h"
#include "TransportBIP15x.h"

namespace {
   const auto kHandshakeTimeout = std::chrono::seconds(5);
}

class Bip15xDataListener : public DataConnectionListener
{
public:
   Bip15xDataConnection *owner_{};
   bool failed_{};

   void OnDataReceived(const std::string& data) override
   {
      if (failed_) {
         return;
      }

      owner_->transport_->onRawDataReceived(data);
   }

   void OnConnected() override
   {
      if (failed_) {
         return;
      }

      if (owner_ != nullptr && owner_->isHandshakeCompleted()) {
         owner_->listener_->OnConnected();
         return;
      }

      owner_->conn_->timer(kHandshakeTimeout, [this] {
         if (!owner_->isHandshakeCompleted()) {
            SPDLOG_LOGGER_DEBUG(owner_->logger_, "close connection because handshake is not complete on time");
            failed_ = true;
            owner_->listener_->OnError(DataConnectionError::ConnectionTimeout);
         }
      });
   }

   void OnDisconnected() override
   {
      if (failed_) {
         return;
      }

      if (owner_->connected_) {
         owner_->connected_ = false;
         owner_->listener_->OnDisconnected();
      }
   }

   void OnError(DataConnectionError errorCode) override
   {
      if (failed_) {
         return;
      }

      OnDisconnected();
      owner_->listener_->OnError(errorCode);
   }
};


Bip15xDataConnection::Bip15xDataConnection(const std::shared_ptr<spdlog::logger> &logger
   , std::unique_ptr<DataConnection> conn
   , const std::shared_ptr<bs::network::TransportClient> &tr)
   : logger_(logger)
   , transport_(tr)
   , conn_(std::move(conn))
{
   assert(conn_);
   transport_->setSendCb([this](const std::string &d) {
      return conn_->send(d);
   });

   transport_->setNotifyDataCb([this](const std::string &d) {
      listener_->OnDataReceived(d);
   });

   transport_->setSocketErrorCb([this](DataConnectionListener::DataConnectionError e) {
      if (e == DataConnectionListener::NoError) {
         {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!sendQueue_.empty()) {
               transport_->sendData(sendQueue_.front());
               sendQueue_.pop();
            }
            connected_ = true;
         }
         listener_->OnConnected();
         return;
      }
      listener_->OnError(e);
   });
}

Bip15xDataConnection::~Bip15xDataConnection()
{
   closeConnection();
}

bool Bip15xDataConnection::openConnection(const std::string &host, const std::string &port
   , DataConnectionListener *listener)
{
   listener_ = listener;
   ownListener_ = std::make_unique<Bip15xDataListener>();
   ownListener_->owner_ = this;
   transport_->openConnection(host, port);
   return conn_->openConnection(host, port, ownListener_.get());
}

bool Bip15xDataConnection::closeConnection()
{
   bool result = conn_->closeConnection();
   transport_->closeConnection();
   return result;
}

bool Bip15xDataConnection::send(const std::string &data)
{
   {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_) {
         sendQueue_.push(data);
         return true;
      }
   }
   return transport_->sendData(data);
}

BinaryData Bip15xDataConnection::getOwnPublicKey() const
{
   auto transportBIP15x = 
      std::dynamic_pointer_cast<bs::network::TransportBIP15x>(transport_);
   if (transportBIP15x == nullptr) {
      throw std::runtime_error("unexpected/null transport ptr type");
   }

   return transportBIP15x->getOwnPubKey();
}

bool Bip15xDataConnection::addCookieKeyToKeyStore(
   const std::string& path, const std::string& name)
{
   auto bip15xClient = std::dynamic_pointer_cast<
      bs::network::TransportBIP15xClient>(transport_);
   
   if (bip15xClient == nullptr) {
      if (logger_) {
         logger_->warn("[Bip15xDataConnection::addCookieKeyToKeyStore] "
            "unexpected transport ptr type");
      }
      return false;
   }

   return bip15xClient->addCookieToPeers(path, name);
}

bool Bip15xDataConnection::usesCookie() const
{
   auto bip15xClient = std::dynamic_pointer_cast<
      bs::network::TransportBIP15xClient>(transport_);

   if (bip15xClient == nullptr) {
      return false;
   }
   return bip15xClient->usesCookie();
}

bool Bip15xDataConnection::isHandshakeCompleted() const
{
   auto bip15xClient = std::dynamic_pointer_cast<
      bs::network::TransportBIP15xClient>(transport_);
   
   if (bip15xClient == nullptr) {
      if (logger_) {
         logger_->warn("[Bip15xDataConnection::isHandshakeCompleted] "
            "unexpected transport ptr type");
      }
      return false;
   }

   return bip15xClient->handshakeCompleted();
 
}