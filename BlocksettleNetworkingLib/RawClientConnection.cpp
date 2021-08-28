#include "RawClientConnection.h"

using namespace bs::network;

StreamServerConnection::server_connection_ptr StreamServerConnection::CreateActiveConnection()
{
   return std::make_shared<ClientConnection<ActiveStreamClient>>(logger_);
}


std::shared_ptr<ServerConnection> bs::network::createServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<ZmqContext>& zmqContext)
{
   return std::make_shared<StreamServerConnection>(logger, zmqContext);
}
