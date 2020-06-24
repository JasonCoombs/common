/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ZmqStreamServerConnection.h"
#include <zmq.h>
#include <spdlog/spdlog.h>
#include "ActiveStreamClient.h"
#include "FastLock.h"
#include "MessageHolder.h"
#include "Transport.h"


ZmqStreamServerConnection::ZmqStreamServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<ZmqContext>& context
   , const std::shared_ptr<bs::network::TransportServer> &tr)
   : ZmqServerConnection(logger, context, tr)
{}

ZmqContext::sock_ptr ZmqStreamServerConnection::CreateDataSocket()
{
   return context_->CreateStreamSocket();
}

bool ZmqStreamServerConnection::ReadFromDataSocket()
{
   // it is client connection. since it is a stream, we will get two frames
   // first - connection ID
   // second - data frame. if data frame is zero length - it means we are connected or disconnected
   MessageHolder id;
   MessageHolder data;

   int result = zmq_msg_recv(&id, dataSocket_.get(), ZMQ_DONTWAIT);
   if (result == -1) {
      logger_->error("[ZmqStreamServerConnection::listenFunction] {} failed to recv ID frame from stream: {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   result = zmq_msg_recv(&data, dataSocket_.get(), ZMQ_DONTWAIT);
   if (result == -1) {
      logger_->error("[ZmqStreamServerConnection::listenFunction] {} failed to recv data frame from stream: {}"
         , connectionName_, zmq_strerror(zmq_errno()));
      return false;
   }

   if (transport_) {
      if (data.GetSize() == 0) {
         return true;
      }
      int socket = zmq_msg_get(&data, ZMQ_SRCFD);

      if (data.GetSize() == bufSizeLimit_) {
         accumulBuf_.append(data.ToString());
         return true;
      }
      else {
         if (accumulBuf_.empty()) {
            transport_->processIncomingData(data.ToString(), id.ToString(), socket);
         }
         else {
            accumulBuf_.append(data.ToString());
            transport_->processIncomingData(accumulBuf_, id.ToString(), socket);
            accumulBuf_.clear();
         }
      }
   }
   else {
      if (data.GetSize() == 0) {
         //we are either connected or disconncted
         onZeroFrame(id.ToString());
      } else {
         onDataFrameReceived(id.ToString(), data.ToString());
      }
   }
   return true;
}

void ZmqStreamServerConnection::onZeroFrame(const std::string& clientId)
{
   bool clientConnected = false;
   {
      FastLock locker(connectionsLockFlag_);

      auto connectionIt = activeConnections_.find(clientId);
      if (connectionIt == activeConnections_.end()) {
         SPDLOG_LOGGER_TRACE(logger_, "have new client connection on {}", connectionName_);

         auto newConnection = CreateActiveConnection();
         newConnection->InitConnection(clientId, this);

         activeConnections_.emplace(clientId, newConnection);

         clientConnected = true;
      } else {
         SPDLOG_LOGGER_TRACE(logger_, "client disconnected on {}", connectionName_);
         activeConnections_.erase(connectionIt);

         clientConnected = false;
      }
   }

   if (clientConnected) {
      notifyListenerOnNewConnection(clientId);
   } else {
      notifyListenerOnDisconnectedClient(clientId);
   }
}

void ZmqStreamServerConnection::onDataFrameReceived(const std::string& clientId, const std::string& data)
{
   auto connection = findConnection(clientId);
   if (connection == nullptr) {
      logger_->error("[ZmqStreamServerConnection::onDataFrameReceived] {} receied data for closed connection {}"
         , connectionName_, clientId);
   } else {
      connection->onRawDataReceived(data);
   }
}

bool ZmqStreamServerConnection::sendRawData(const std::string& clientId, const std::string& rawData)
{
   if (!isActive()) {
      logger_->error("[ZmqStreamServerConnection::sendRawData] cound not send. not connected");
      return false;
   }

   QueueDataToSend(clientId, rawData, true);

   return true;
}

bool ZmqStreamServerConnection::SendDataToClient(const std::string& clientId, const std::string& data)
{
   if (transport_) {
      return transport_->sendData(clientId, data);
   }
   else {
      auto connection = findConnection(clientId);
      if (connection == nullptr) {
         logger_->error("[ZmqStreamServerConnection::SendDataToClient] {} send data to closed connection {}"
            , connectionName_, clientId);
         return false;
      }
      return connection->send(data);
   }
}

bool ZmqStreamServerConnection::SendDataToAllClients(const std::string& data)
{
   unsigned int successCount = 0;

   if (transport_) {
      for (const auto &client : clientInfo_) {
         if (transport_->sendData(client.first, data)) {
            successCount++;
         }
      }
      return (successCount == clientInfo_.size());
   }
   else {
      FastLock locker(connectionsLockFlag_);
      for (const auto &it : activeConnections_) {
         const bool result = it.second->send(data);
         if (result) {
            successCount++;
         }
      }
      return (successCount == activeConnections_.size());
   }
}

ZmqStreamServerConnection::server_connection_ptr
ZmqStreamServerConnection::findConnection(const std::string& clientId)
{
   FastLock locker(connectionsLockFlag_);
   auto connectionIt = activeConnections_.find(clientId);
   if (connectionIt == activeConnections_.end()) {
      return nullptr;
   }

   return connectionIt->second;
}
