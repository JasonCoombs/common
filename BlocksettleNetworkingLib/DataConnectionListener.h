/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __DATA_CONNECTION_LISTENER_H__
#define __DATA_CONNECTION_LISTENER_H__

#include <stdexcept>
#include <string>

class DataConnectionListener
{
public:
   enum DataConnectionError
   {
      NoError,
      UndefinedSocketError,
      HostNotFoundError,
      HandshakeFailed,
      SerializationFailed,
      HeartbeatWaitFailed,
      ConnectionTimeout,
      ProtocolViolation,
   };

public:
   DataConnectionListener() = default;
   virtual ~DataConnectionListener() noexcept = default;

   DataConnectionListener(const DataConnectionListener&) = delete;
   DataConnectionListener& operator = (const DataConnectionListener&) = delete;

   DataConnectionListener(DataConnectionListener&&) = delete;
   DataConnectionListener& operator = (DataConnectionListener&&) = delete;

public:
   virtual void OnDataReceived(const std::string& data) = 0;
   virtual void OnConnected() = 0;
   virtual void OnDisconnected() = 0;
   virtual void OnError(DataConnectionError errorCode) = 0;
};


class DataTopicListener : public DataConnectionListener
{
public:
   DataTopicListener() = default;

   void OnDataReceived(const std::string& data) override final
   {
      throw std::runtime_error("not supported");
   }
   virtual void OnDataReceived(const std::string& topic, const std::string& data) = 0;
};

#endif // __DATA_CONNECTION_LISTENER_H__
