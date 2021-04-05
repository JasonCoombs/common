/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_GET_ASSIGNED_ACCOUNTS_H__
#define __CELER_GET_ASSIGNED_ACCOUNTS_H__

#include "CommandSequence.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class GetAssignedAccountsListSequence : public CommandSequence<GetAssignedAccountsListSequence>
      {
      public:
         using onGetAccountListFunc = std::function< void(const std::vector<std::string>& assignedAccounts)>;

         GetAssignedAccountsListSequence(const std::shared_ptr<spdlog::logger>&
            , const onGetAccountListFunc& cb);
         ~GetAssignedAccountsListSequence() = default;

         bool FinishSequence() override;

         CelerMessage sendFindAccountRequest();
         bool         processFindAccountResponse(const CelerMessage&);

      private:
         std::shared_ptr<spdlog::logger> logger_;
         onGetAccountListFunc cb_;

         std::vector<std::string> assignedAccounts_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_GET_ASSIGNED_ACCOUNTS_H__