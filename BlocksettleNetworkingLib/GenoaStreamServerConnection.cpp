/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "GenoaStreamServerConnection.h"

#include "ActiveStreamClient.h"
#include "GenoaConnection.h"
#include "Transport.h"


GenoaStreamServerConnection::GenoaStreamServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<ZmqContext>& context
   , const std::shared_ptr<bs::network::TransportServer> &t)
   : ZmqStreamServerConnection(logger, context, t)
{}

ZmqStreamServerConnection::server_connection_ptr GenoaStreamServerConnection::CreateActiveConnection()
{
   return std::make_shared<GenoaConnection<ActiveStreamClient>>(logger_);
}
