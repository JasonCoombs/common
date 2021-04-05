/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_LOGIN_SEQUENCE_H__
#define __CELER_LOGIN_SEQUENCE_H__

#include "CommandSequence.h"

#include <chrono>
#include <functional>
#include <string>

#include <spdlog/spdlog.h>

namespace bs {
   namespace celer {
      class LoginSequence : public CommandSequence<LoginSequence>
      {
      public:
         using onLoginSuccess_func = std::function<void(const std::string& sessionToken
            , std::chrono::seconds heartbeatInterval)>;
         using onLoginFailed_func = std::function<void(const std::string& errorMessage)>;

      public:
         LoginSequence(const std::shared_ptr<spdlog::logger>&
            , const std::string& username, const std::string& password);
         ~LoginSequence() noexcept override = default;

         LoginSequence(const LoginSequence&) = delete;
         LoginSequence& operator = (const LoginSequence&) = delete;

         LoginSequence(LoginSequence&&) = delete;
         LoginSequence& operator = (LoginSequence&&) = delete;

         bool FinishSequence() override;

         void SetCallbackFunctions(const onLoginSuccess_func& onSuccess
            , const onLoginFailed_func& onFailed);

      private:
         CelerMessage sendLoginRequest();
         bool         processLoginResponse(const CelerMessage&);
         bool         processConnectedEvent(const CelerMessage&);

         std::shared_ptr<spdlog::logger> logger_;
         std::string username_;
         std::string password_;

         std::string errorMessage_;
         std::chrono::seconds heartbeatInterval_;
         std::string sessionToken_;

         onLoginFailed_func   onLoginFailed_;
         onLoginSuccess_func  onLoginSuccess_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_LOGIN_SEQUENCE_H__
