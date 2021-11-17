#ifndef __LAMBDA_SERVER_LISTENER_H__
#define __LAMBDA_SERVER_LISTENER_H__

#include "ServerConnectionListener.h"

#include <functional>

class LambdaServerListener : public ServerConnectionListener
{
public:
   using onDataCB = std::function<void (const std::string&, const std::string& )>;
   using onClientConnectedCB = std::function<void (const std::string&, const Details&)>;
   using onClientDisconnectedCB = std::function<void (const std::string& )>;
   using onClientErrorCB= std::function<void(const std::string &, ClientError, const Details&)>;

public:
   LambdaServerListener() = default;
   ~LambdaServerListener() noexcept override = default;

   LambdaServerListener(const LambdaServerListener&) = delete;
   LambdaServerListener& operator = (const LambdaServerListener&) = delete;

   LambdaServerListener(LambdaServerListener&&) = delete;
   LambdaServerListener& operator = (LambdaServerListener&&) = delete;

public:
   void setOnData(const onDataCB& onData);
   void setOnClientConnected(const onClientConnectedCB& onClientConnected);
   void setOnClientDisconnected(const onClientDisconnectedCB& onClientDisconnected);
   void setOnClientError(const onClientErrorCB& onClientError);

private:
   void OnDataFromClient(const std::string& clientId, const std::string& data) override;
   void OnClientConnected(const std::string& clientId, const Details& details) override;
   void OnClientDisconnected(const std::string& clientId) override;
   void onClientError(const std::string& clientId, ClientError error, const Details& details) override;

private:
   onDataCB                onData_;
   onClientConnectedCB     onClientConnected_;
   onClientDisconnectedCB  onClientDisconnected_;
   onClientErrorCB         onClientError_;
};

#endif // __LAMBDA_SERVER_LISTENER_H__
