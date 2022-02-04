/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "LoginServerConnection.h"
#ifdef WIN32
#include <Winsock2.h>
#else // WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <unordered_set>
#include <openssl/err.h>
#include <openssl/ossl_typ.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "BtcUtils.h"
#include "StringUtils.h"

using json = nlohmann::json;

LoginServerConnection::LoginServerConnection(const std::shared_ptr<spdlog::logger>& logger
   , const std::string& host, LoginServerListener* listener)
   : HttpsConnection(logger, host), listener_(listener)
{}

void LoginServerConnection::refreshToken(const std::string& token)
{
   if (token.empty()) {
      logger_->error("[{}] skip empty token", __func__);
      return;
   }
   inRequest_ = true;
   pendingRequest_ = RequestType::RefreshToken;
   const json req{ {"access_token", token} };
   const auto& body = req.dump();
   const std::vector<std::string> headers{ "Content-Type: application/json; charset=utf-8"
      , "Content-Length: " + std::to_string(body.length()) };
   sendPostRequest("/api/v1/session", body, headers);

   while (inRequest_) {
      std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
   }
}

void LoginServerConnection::renewToken(const std::string& token)
{
   inRequest_ = true;
   pendingRequest_ = RequestType::RenewToken;
   const json req{ {"signed_challenge", token} };
   const auto& body = req.dump();
   const std::vector<std::string> headers{ "Content-Type: application/json; charset=utf-8"
      , "Content-Length: " + std::to_string(body.length()) };
   sendPostRequest("/api/v1/token", body, headers);

   while (inRequest_) {
      std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
   }
}

void LoginServerConnection::process(const std::string& data)
{
   if (data.empty()) {
      return;
   }
   logger_->debug("[LoginServerConnection::process] received:\n{}", data);
   auto itBody = data.find("\r\n\r\n");
   std::string body;
   if (itBody == std::string::npos) {
      body = data;
   }
   else {
      body = data.substr(itBody + 4);
      if (body.empty()) {
         return;
      }
   }
   if (!bodyLen_) {
      itBody = body.find("\r\n");
      if (itBody != std::string::npos) {
         bodyLen_ = bs::fromHex(body.substr(0, itBody));
         body = body.substr(itBody + 2);  // skip length
      }
   }
   body_ += body;
   bodyLen_ -= std::min(bodyLen_, body.length());
   if (bodyLen_ > 0) {
      return;  // wait for next chunk
   }
   logger_->debug("[LoginServerConnection] body: {}", body_);
   assert(listener_ != nullptr);
   const json message = json::parse(body_);

   if (message.contains("error") && message["error"].get<bool>()) {
      logger_->error("[LoginServerConnection] error {}: {}"
         , message.contains("error_code") ? message["error_code"].get<std::string>() : ""
         , message["error_message"].get<std::string>());
   }
   else if (message.contains("access_token")) {
      switch (pendingRequest_) {
      case RequestType::RefreshToken:
         listener_->onTokenRefreshed(message["access_token"].get<std::string>());
         break;
      case RequestType::RenewToken:
         listener_->onNewToken(message["access_token"].get<std::string>());
         break;
      default:
         logger_->error("[{}] unknown request {}", __func__, pendingRequest_);
         break;
      }
      pendingRequest_ = RequestType::Unknown;
   }
   inRequest_ = false;
}
