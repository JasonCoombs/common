/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef LOGIN_SERVER_CONNECTION_H
#define LOGIN_SERVER_CONNECTION_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifndef WIN32
#include "netdb.h"
#endif
#ifdef WITH_CJOSE
#include <cjose/cjose.h>
#endif
#include <openssl/ssl.h>
#include "HttpsConnection.h"

namespace spdlog {
   class logger;
}

class LoginServerListener
{
public:
   virtual void onTokenRefreshed(const std::string&) = 0;
   virtual void onNewToken(const std::string&) = 0;
};


class LoginServerConnection : public HttpsConnection
{
public:
   LoginServerConnection(const std::shared_ptr<spdlog::logger>&
      , const std::string& host, LoginServerListener *);
   ~LoginServerConnection() = default;

   void refreshToken(const std::string&);
   void renewToken(const std::string&);

protected:
   void process(const std::string&) override;

private:
   LoginServerListener* listener_{ nullptr };
   size_t         bodyLen_{ 0 };
   std::string    body_;

   enum class RequestType {
      Unknown,
      RefreshToken,
      RenewToken
   };
   RequestType pendingRequest_{ RequestType::Unknown };
};

#endif // LOGIN_SERVER_CONNECTION_H
