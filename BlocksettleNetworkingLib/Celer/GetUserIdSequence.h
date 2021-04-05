/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef CELERGETUSERIDSEQUENCE_H
#define CELERGETUSERIDSEQUENCE_H

#include "CommandSequence.h"
#include <memory>
#include <functional>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class GetUserIdSequence : public CommandSequence<GetUserIdSequence>
      {
      public:
         using onGetId_func = std::function< void(const std::string& userId)>;

         GetUserIdSequence(const std::shared_ptr<spdlog::logger>&
            , const std::string& username, const onGetId_func& cb);
         ~GetUserIdSequence() = default;

         bool FinishSequence() override;
         CelerMessage sendGetUserIdRequest();
         bool         processGetUserIdResponse(const CelerMessage&);

      private:
         std::shared_ptr<spdlog::logger> logger_;
         onGetId_func cb_;
         std::string username_;
         std::string userId_;
      };

   }  //namespace celer
}  //namespace bs

#endif // CELERGETUSERIDSEQUENCE_H
