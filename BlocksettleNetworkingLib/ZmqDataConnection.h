/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __ZEROMQ_DATA_CONNECTION_H__
#define __ZEROMQ_DATA_CONNECTION_H__

#include "DataConnection.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include "BinaryData.h"
#include "ZmqContext.h"

namespace spdlog{ 
   class logger;
}
namespace bs {
   namespace network {
      class TransportClient;
   }
}


class ZmqDataConnection : public DataConnection
{
public:
   ZmqDataConnection(const std::shared_ptr<spdlog::logger>& logger
      , const std::shared_ptr<bs::network::TransportClient> &tr = nullptr
      , bool useMonitor = false);
   ~ZmqDataConnection() noexcept override;

   ZmqDataConnection(const ZmqDataConnection&) = delete;
   ZmqDataConnection& operator = (const ZmqDataConnection&) = delete;

   ZmqDataConnection(ZmqDataConnection&&) = delete;
   ZmqDataConnection& operator = (ZmqDataConnection&&) = delete;

   void SetContext(const std::shared_ptr<ZmqContext>& context) {
      context_ = context;
   }

public:
   bool openConnection(const std::string& host
                     , const std::string& port
                     , DataConnectionListener* listener) override;
   bool closeConnection() override;
   bool isActive() const override;

   bool SetZMQTransport(ZMQTransport transport);

protected:
   bool sendRawData(const std::string& rawData);
   bool sendData(const std::string &);

   virtual bool recvData();

   virtual ZmqContext::sock_ptr CreateDataSocket();
   virtual bool ConfigureDataSocket(const ZmqContext::sock_ptr& socket);

private:
   void resetConnectionObjects();

   // run in thread
   void listenFunction();

   // socket monitor is not added. so will use 0 frame as notification
   void zeroFrameReceived();

   void onError(DataConnectionListener::DataConnectionError);
   void onDisconnected();
   void onConnected();

private:
   enum SocketIndex {
      ControlSocketIndex = 0,
      StreamSocketIndex,
      MonitorSocketIndex
   };

   enum InternalCommandCode {
      CommandSend = 0,
      CommandStop
   };

protected:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<bs::network::TransportClient>   transport_;
   const bool                       useMonitor_;
   std::string                      connectionName_;

   std::shared_ptr<ZmqContext>      context_;
   std::atomic_flag                 lockFlag_ = ATOMIC_FLAG_INIT;
   std::atomic_flag                 controlSocketLock_ = ATOMIC_FLAG_INIT;
   ZmqContext::sock_ptr             dataSocket_;
   ZmqContext::sock_ptr             monSocket_;
   std::string                      hostAddr_;
   std::string                      hostPort_;

private:
   std::string                      socketId_;

   std::thread                      listenThread_;

   ZmqContext::sock_ptr             threadMasterSocket_;
   ZmqContext::sock_ptr             threadSlaveSocket_;

   bool                             isConnected_{ false };

   std::vector<std::string>         sendQueue_;

   ZMQTransport                     zmqTransport_ = ZMQTransport::TCPTransport;

   std::shared_ptr<bool>            continueExecution_ = nullptr;

   size_t         bufSizeLimit_{ 8192 };
   std::string    accumulBuf_;
};


class ZmqBinaryConnection : public ZmqDataConnection
{
public:
   ZmqBinaryConnection(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<bs::network::TransportClient> &t)
      : ZmqDataConnection(logger, t, true)
   {}

   ~ZmqBinaryConnection() noexcept override = default;

   ZmqBinaryConnection(const ZmqBinaryConnection&) = delete;
   ZmqBinaryConnection& operator = (const ZmqBinaryConnection&) = delete;
   ZmqBinaryConnection(ZmqBinaryConnection&&) = delete;
   ZmqBinaryConnection& operator = (ZmqBinaryConnection&&) = delete;

public:
   bool send(const std::string& data) override
   {
      return ZmqDataConnection::sendData(data);
   }

protected:
   void onRawDataReceived(const std::string &rawData) override
   {
      ZmqDataConnection::notifyOnData(rawData);
   }
};

#endif // __ZEROMQ_DATA_CONNECTION_H__
