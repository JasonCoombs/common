#include "LambdaServerListener.h"

void LambdaServerListener::setOnData(const onDataCB& onData)
{
   onData_ = onData;
}

void LambdaServerListener::setOnClientConnected(const onClientConnectedCB& onClientConnected)
{
   onClientConnected_ = onClientConnected;
}

void LambdaServerListener::setOnClientDisconnected(const onClientDisconnectedCB& onClientDisconnected)
{
   onClientDisconnected_ = onClientDisconnected;
}

void LambdaServerListener::setOnClientError(const onClientErrorCB& onClientError)
{
   onClientError_ = onClientError;
}

void LambdaServerListener::OnDataFromClient(const std::string& clientId, const std::string& data)
{
   if (onData_) {
      onData_(clientId, data);
   }
}

void LambdaServerListener::OnClientConnected(const std::string &clientId, const Details& details)
{
   if (onClientConnected_) {
      onClientConnected_(clientId, details);
   }
}

void LambdaServerListener::OnClientDisconnected(const std::string& clientId)
{
   if (onClientDisconnected_) {
      onClientDisconnected_(clientId);
   }
}

void LambdaServerListener::onClientError(const std::string &clientId, ClientError error, const Details& details)
{
   if (onClientError_) {
      onClientError_(clientId, error, details);
   }
}
