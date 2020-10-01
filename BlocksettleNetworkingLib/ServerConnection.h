/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __SERVER_CONNECTION_H__
#define __SERVER_CONNECTION_H__

#include "ServerConnectionListener.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

class ServerConnection
{
public:
   ServerConnection() = default;
   virtual ~ServerConnection() noexcept = default;

   ServerConnection(const ServerConnection&) = delete;
   ServerConnection& operator = (const ServerConnection&) = delete;

   ServerConnection(ServerConnection&&) = delete;
   ServerConnection& operator = (ServerConnection&&) = delete;

   virtual bool BindConnection(const std::string& host, const std::string& port
      , ServerConnectionListener* listener) = 0;

   virtual bool SendDataToClient(const std::string& clientId, const std::string& data) = 0;
   virtual bool SendDataToAllClients(const std::string&) { return false; }

   // Execute callback after timeout on listening thread
   using TimerCallback = std::function<void()>;
   virtual bool timer(std::chrono::milliseconds /*timeout*/, TimerCallback /*callback*/) { return false; }

   // Close client connection
   virtual bool closeClient(const std::string& /*clientId*/) { return false; }
};

#endif // __SERVER_CONNECTION_H__
