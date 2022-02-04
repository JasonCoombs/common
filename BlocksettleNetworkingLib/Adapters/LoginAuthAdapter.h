/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef LOGIN_AUTH_ADAPTER_H
#define LOGIN_AUTH_ADAPTER_H

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
#include "LoginServerConnection.h"
#include "Message/ThreadedAdapter.h"

namespace spdlog {
   class logger;
}

class LoginAuthAdapter : public bs::message::ThreadedAdapter, public LoginServerListener
{
public:
   LoginAuthAdapter(const std::shared_ptr<spdlog::logger>&
      , const std::shared_ptr<bs::message::User>&
      , const std::string& host, const std::string& privKeyFile
      , const std::string& serviceURL);
   ~LoginAuthAdapter();

   std::set<std::shared_ptr<bs::message::User>> supportedReceivers() const override
   {
      return { user_ };
   }
   std::string name() const override { return "LoginAuth"; }

protected:
   bool processEnvelope(const bs::message::Envelope&) override;
   void onTokenRefreshed(const std::string&) override;
   void onNewToken(const std::string&) override;

private:
   void processRefreshToken(const std::string&);
   void processRenewToken();

private:
   std::shared_ptr<spdlog::logger>     logger_;
   std::shared_ptr<bs::message::User>  user_;
   const std::string    host_;
   const std::string    serviceURL_;
#ifdef WITH_CJOSE
   cjose_jwk_t*   jwk_;
#endif
   std::string    pubKeyId_;
   bs::message::Envelope   envReq_;
};

#endif // LOGIN_AUTH_ADAPTER_H
