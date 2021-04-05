/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_GET_USER_PROPERTY_SEQUENCE_H__
#define __CELER_GET_USER_PROPERTY_SEQUENCE_H__

#include "CommandSequence.h"
#include <memory>
#include <functional>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class GetUserPropertySequence : public CommandSequence<GetUserPropertySequence>
      {
      public:
         using onGetProperty_func = std::function< void(const std::string& value, int64_t id)>;

         GetUserPropertySequence(const std::shared_ptr<spdlog::logger>&
            , const std::string& username, const std::string& propertyName
            , const onGetProperty_func& cb);
         ~GetUserPropertySequence() = default;

         bool FinishSequence() override;
         CelerMessage sendFindPropertyRequest();
         bool         processGetPropertyResponse(const CelerMessage&);

      private:
         std::shared_ptr<spdlog::logger> logger_;
         onGetProperty_func cb_;
         const std::string username_;
         const std::string propertyName_;
         std::string       value_;
         int64_t           id_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_GET_USER_PROPERTY_SEQUENCE_H__
