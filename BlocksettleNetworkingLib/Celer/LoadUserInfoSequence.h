/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_LOAD_USER_INFO_SEQUENCE_H__
#define __CELER_LOAD_USER_INFO_SEQUENCE_H__

#include "CommandSequence.h"
#include "Property.h"

#include <memory>
#include <functional>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class LoadUserInfoSequence : public CommandSequence<LoadUserInfoSequence>
      {
      public:
         using onPropertiesRecvd_func = std::function<void(Properties properties)>;

         LoadUserInfoSequence(const std::shared_ptr<spdlog::logger>&
            , const std::string& username
            , const onPropertiesRecvd_func& cb);
         ~LoadUserInfoSequence() = default;

         bool FinishSequence() override;

      private:
         CelerMessage sendGetUserIdRequest();
         CelerMessage sendGetSubmittedAuthAddressListRequest();
         CelerMessage sendGetSubmittedCCAddressListRequest();
         CelerMessage sendGetBitcoinParticipantRequest();
         CelerMessage sendGetBitcoinDealerRequest();

         CelerMessage   getPropertyRequest(const std::string& name);
         bool           processGetPropertyResponse(const CelerMessage& message);

      private:
         std::shared_ptr<spdlog::logger> logger_;
         onPropertiesRecvd_func  cb_;
         const std::string username_;
         Properties        properties_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_LOAD_USER_INFO_SEQUENCE_H__
