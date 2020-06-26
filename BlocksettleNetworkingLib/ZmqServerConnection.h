/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __ZEROMQ_SERVER_CONNECTION_H__
#define __ZEROMQ_SERVER_CONNECTION_H__

#include "ServerConnection.h"
#include "ZmqContext.h"

#include <atomic>
#include <deque>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bs {
   namespace network {
      class TransportServer;
   }
}
namespace spdlog {
   class logger;
}

class ZmqServerConnection : public ServerConnection
{
public:
   ZmqServerConnection(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<ZmqContext> &
      , const std::shared_ptr<bs::network::TransportServer> &tr = nullptr);

   ~ZmqServerConnection() noexcept override;

   ZmqServerConnection(const ZmqServerConnection&) = delete;
   ZmqServerConnection& operator = (const ZmqServerConnection&) = delete;

   ZmqServerConnection(ZmqServerConnection&&) = delete;
   ZmqServerConnection& operator = (ZmqServerConnection&&) = delete;

   bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) override;

   std::string GetClientInfo(const std::string &clientId) const override;

   void setTransport(const std::shared_ptr<bs::network::TransportServer> &) override;

   bool SetZMQTransport(ZMQTransport transport);

   void SetImmediate(bool flag = true) { immediate_ = flag; }
   void SetIdentity(const std::string &id) { identity_ = id; }

   // Sets list of addresses (in ipv4 or ipv6 CIDR) from which we would accept incoming TCP connections.
   // If not set filtering is not applied.
   // Make sure to set it before BindConnection call.
   void setListenFrom(const std::vector<std::string> &fromAddresses);

   void setThreadName(const std::string &name);

protected:
   bool isActive() const;

   // interface for active connection listener
   void notifyListenerOnData(const std::string& clientId, const std::string& data);

   void notifyListenerOnNewConnection(const std::string& clientId);
   void notifyListenerOnDisconnectedClient(const std::string& clientId);
   void notifyListenerOnClientError(const std::string& clientId, const std::string &error);
   void notifyListenerOnClientSocketError(const std::string& clientId
      , ServerConnectionListener::ClientError errorCode, int socket);

   virtual ZmqContext::sock_ptr CreateDataSocket() = 0;
   virtual bool ConfigDataSocket(const ZmqContext::sock_ptr& dataSocket);

   virtual bool ReadFromDataSocket() = 0;

   virtual void onPeriodicCheck();

   virtual bool QueueDataToSend(const std::string& clientId, const std::string& data, bool sendMore);

protected:
   void stopServer();

   void requestPeriodicCheck();
   std::thread::id listenThreadId() const;

private:
   // run in thread
   void listenFunction();

   bool SendDataCommand();
   void SendDataToDataSocket();

protected:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<ZmqContext>      context_;
   std::shared_ptr<bs::network::TransportServer>   transport_;
   std::string                      connectionName_;

   // should be accessed only from overloaded ReadFromDataSocket.
   ZmqContext::sock_ptr             dataSocket_;
   ZmqContext::sock_ptr             monSocket_;

   std::unordered_map<std::string, std::string> clientInfo_; // ClientID & related string
   size_t   bufSizeLimit_{ 8192 };

private:
   enum SocketIndex {
      ControlSocketIndex = 0,
      DataSocketIndex,
      MonitorSocketIndex
   };

   enum InternalCommandCode {
      CommandSend = 0,
      CommandStop
   };

   struct DataToSend
   {
      std::string    clientId;
      std::string    data;
      bool           sendMore;
   };

   std::thread                      listenThread_;
   std::atomic_flag                 controlSocketLockFlag_ = ATOMIC_FLAG_INIT;
   ZmqContext::sock_ptr             threadMasterSocket_;
   ZmqContext::sock_ptr             threadSlaveSocket_;
   ServerConnectionListener*        listener_{nullptr};
   std::atomic_flag                 dataQueueLock_ = ATOMIC_FLAG_INIT;
   std::deque<DataToSend>           dataQueue_;
   ZMQTransport                     zmqTransport_ = ZMQTransport::TCPTransport;
   std::unordered_map<int, std::string> connectedPeers_;
   std::string                      monitorConnectionName_;
   bool        immediate_{ false };
   std::string identity_;
   int sendTimeoutInMs_{ 5000 };
   std::vector<std::string> fromAddresses_;
   std::string threadName_;
};

#endif // __ZEROMQ_SERVER_CONNECTION_H__
