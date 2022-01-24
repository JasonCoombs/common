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

namespace spdlog
{
   class logger;
}

#include "ZmqContext.h"

class ZmqDataConnection : public DataConnection
{
public:
   ZmqDataConnection(const std::shared_ptr<spdlog::logger>& logger, bool useMonitor = false);
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
   bool isActive() const;

   bool SetZMQTransport(ZMQTransport transport);

protected:
   bool sendRawData(const std::string& rawData);

   virtual bool recvData();

   virtual ZmqContext::sock_ptr CreateDataSocket();
   virtual bool ConfigureDataSocket(const ZmqContext::sock_ptr& socket);

   // socket monitor is not added. so will use 0 frame as notification
   void zeroFrameReceived();

private:
   void resetConnectionObjects();

   // run in thread
   void listenFunction();

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

   bool                             isConnected_;

   std::vector<std::string>         sendQueue_;

   ZMQTransport                     zmqTransport_ = ZMQTransport::TCPTransport;

   std::shared_ptr<bool>            continueExecution_ = nullptr;
};


class ZmqSubConnection : public ZmqDataConnection
{
public:
   ZmqSubConnection(const std::shared_ptr<spdlog::logger>& logger, bool useMonitor = false)
      : ZmqDataConnection(logger, useMonitor) {}
   ~ZmqSubConnection() noexcept override = default;

   void subscribeTopics(const std::vector<std::string>& topics)
   {
      topics_ = topics;
   }
   bool ConfigureDataSocket(const ZmqContext::sock_ptr&) override;
   bool openConnection(const std::string& host, const std::string& port
      , DataConnectionListener* listener) override
   {
      throw std::runtime_error("not supported");
   }
   virtual bool openConnection(const std::string& host, const std::string& port
      , DataTopicListener*);
   bool send(const std::string& data) override { return false; }  // can't send, only subscribe

protected:
   ZmqContext::sock_ptr CreateDataSocket() override;
   bool recvData() override;

protected:
   DataTopicListener* topicListener_{ nullptr };
   std::vector<std::string>   topics_;
};

#endif // __ZEROMQ_DATA_CONNECTION_H__
