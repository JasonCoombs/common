/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "LoginAuthAdapter.h"
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
#include "login_auth.pb.h"

using json = nlohmann::json;
using namespace BlockSettle;


LoginAuthAdapter::LoginAuthAdapter(const std::shared_ptr<spdlog::logger>& logger
   , const std::shared_ptr<bs::message::User>& user
   , const std::string& host, const std::string& privKeyFile
   , const std::string& serviceURL)
   : logger_(logger), user_(user), host_(host), serviceURL_(serviceURL)
{
   auto f = fopen(privKeyFile.c_str(), "rt");
   if (!f) {
      throw std::runtime_error("failed to open private key file " + privKeyFile);
   }
   auto privKey = PEM_read_ECPrivateKey(f, NULL, NULL, NULL);
   fclose(f);
   if (!privKey) {
      throw std::runtime_error("failed to read private key " + privKeyFile);
   }
   uint8_t pKey[64];
   const auto privBigNum = EC_KEY_get0_private_key(privKey);
   const auto pKeySize = BN_bn2bin(privBigNum, (unsigned char*)pKey);
   if (pKeySize != 32) {
      throw std::runtime_error("invalid private key size: " + std::to_string(pKeySize));
   }

   const auto ecPoint = EC_KEY_get0_public_key(privKey);
   if (!ecPoint) {
      throw std::runtime_error("can't get EC point");
   }
#ifdef WITH_CJOSE
   cjose_err cjoseErr;
   cjose_jwk_ec_keyspec ecKeySpec{ CJOSE_JWK_EC_P_256, pKey, pKeySize, NULL, 0, NULL, 0 };   // don't pass x and y

   jwk_ = cjose_jwk_create_EC_spec(&ecKeySpec, &cjoseErr);
   if (!jwk_) {
      throw std::runtime_error("failed to create JWK: " + std::string(cjoseErr.message));
   }
   auto jsonOut = cjose_jwk_to_json(jwk_, false, &cjoseErr);
   if (!jsonOut) {
      throw std::runtime_error("no json output for JWK");
   }
   const auto& jsonKey = nlohmann::json::parse(jsonOut);
   free(jsonOut);              // need to re-assemble in proper order
   const std::string thumbprintData = "{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\""
      + jsonKey["x"].get<std::string>() + "\",\"y\":\"" + jsonKey["y"].get<std::string>() + "\"}";

   auto kid = BtcUtils::base64_encode(BtcUtils::getSha256(
      BinaryData{(uint8_t const*)thumbprintData.data(), thumbprintData.size()}).toBinStr());
   std::replace(kid.begin(), kid.end(), '+', '-'); // hand-made base64url
   std::replace(kid.begin(), kid.end(), '/', '_');
   while (kid.find_last_of('=') != std::string::npos) {
      kid.pop_back();
   }
   if (!cjose_jwk_set_kid(jwk_, kid.c_str(), kid.length(), &cjoseErr)) {
      throw std::runtime_error("failed to set kid " + kid);
   }
   pubKeyId_ = kid;

   jsonOut = cjose_jwk_to_json(jwk_, false, &cjoseErr);
   logger->debug("[LoginService] JWK: {}", (jsonOut == NULL) ? "null" : jsonOut);
   free(jsonOut);
#else
   throw std::runtime_error("can't init LoginAuth without cjose");
#endif
}

LoginAuthAdapter::~LoginAuthAdapter()
{
#ifdef WITH_CJOSE
   cjose_jwk_release(jwk_);
#endif
}

bool LoginAuthAdapter::processEnvelope(const bs::message::Envelope& env)
{
   if (env.sender->isSystem()) {
      return true;   // don't handle system start
   }
   if (env.isRequest()) {
      envReq_ = env;    // requests are processed only synchronously
      LoginAuth::Message msg;
      if (!msg.ParseFromString(env.message)) {
         logger_->error("[LoginAuthAdapter::processEnvelope] failed to parse msg #{}", env.foreignId());
         return true;
      }
      switch (msg.data_case()) {
      case LoginAuth::Message::kRenewRequest:
         processRenewToken();
         break;
      case LoginAuth::Message::kRefreshRequest:
         processRefreshToken(msg.refresh_request());
         break;
      default:
         logger_->error("[LoginAuthAdapter::processEnvelope] unknown request {} in #{}"
            , msg.data_case(), env.foreignId());
         break;
      }
      return true;
   }
   return true;
}

void LoginAuthAdapter::onTokenRefreshed(const std::string& token)
{
   LoginAuth::Message msg;
   msg.set_refresh_response(token);
   pushResponse(user_, envReq_, msg.SerializeAsString());
   logger_->debug("[{}] {} sent to {}", __func__, token, envReq_.sender->name());
}

void LoginAuthAdapter::onNewToken(const std::string& token)
{
   LoginAuth::Message msg;
   msg.set_renew_response(token);
   pushResponse(user_, envReq_, msg.SerializeAsString());
   logger_->debug("[{}] {} sent to {}", __func__, token, envReq_.sender->name());
}

void LoginAuthAdapter::processRefreshToken(const std::string& token)
{
   try {
      LoginServerConnection conn(logger_, host_, this);
      conn.refreshToken(token);
      logger_->debug("[{}] finished", __func__);
   }
   catch (const std::exception& e) {
      logger_->error("[{}] HTTPS connection error: ", __func__, e.what());
   }
}

void LoginAuthAdapter::processRenewToken()
{
   const auto& timeNow = std::chrono::system_clock::now();
   const auto& nowC = std::chrono::system_clock::to_time_t(timeNow);
   const auto& tmNow = *std::gmtime(&nowC);
   char buf[128];
   std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmNow);
   const std::string timestamp = buf;
   const json token{ {"thumbprint", pubKeyId_}, {"service_url", serviceURL_ }
      , {"created", timestamp }};
   const auto& tokenStr = token.dump();
#ifdef WITH_CJOSE
   cjose_err cjoseErr;
   cjose_header_t* header = cjose_header_new(&cjoseErr);
   cjose_header_set(header, CJOSE_HDR_ALG, "ES256", &cjoseErr);
   cjose_header_set(header, CJOSE_HDR_KID, pubKeyId_.c_str(), &cjoseErr);
   const auto jws = cjose_jws_sign(jwk_, header, (uint8_t*)tokenStr.c_str(), tokenStr.length(), &cjoseErr);
   cjose_header_release(header);
   if (!jws) {
      logger_->error("[{}] failed to sign", __func__);
      return;
   }
   const char* signedToken = NULL;
   if (!cjose_jws_export(jws, &signedToken, &cjoseErr)) {
      logger_->error("[{}] failed to export signed token", __func__);
      cjose_jws_release(jws);
      return;
   }

   try {
      LoginServerConnection conn(logger_, host_, this);
      conn.renewToken(signedToken);
      logger_->debug("[{}] finished", __func__);
   } catch (const std::exception& e) {
      logger_->error("[{}] HTTPS connection error: ", __func__, e.what());
   }
   cjose_jws_release(jws);
#endif
}
