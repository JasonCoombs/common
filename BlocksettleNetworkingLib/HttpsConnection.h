/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef HTTPS_CONNECTION_H
#define HTTPS_CONNECTION_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifndef WIN32
#include "netdb.h"
#endif
#include <openssl/ssl.h>

namespace spdlog {
   class logger;
}

extern const char* version;

class HttpsConnection
{
public:
   HttpsConnection(const std::shared_ptr<spdlog::logger> &, const std::string &host);
   ~HttpsConnection();

   void sendPostRequest(const std::string&, const std::string& body = {}
      , const std::vector<std::string>& additionalHeaders = {});

protected:
   virtual void process(const std::string&) = 0;
   void sendGetRequest(const std::string&, const std::vector<std::string>& additionalHeaders = {});
   void sendRequest(const std::string&);
   void disconnectSocket();

private:
   std::string readSocket();
   void connectSocket();

protected:
   std::shared_ptr<spdlog::logger>  logger_;
   const std::string host_;
   SSL_CTX  *ctx_{ nullptr };
   SSL      *ssl_{ nullptr };
   std::atomic_bool  stopped_{ false };
   std::atomic_bool  inRequest_{ true };

   std::unordered_map<std::string, std::string> accounts_;
};

#endif // HTTPS_CONNECTION_H
