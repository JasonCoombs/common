/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __FIND_SUBLEDGERS_SEQUENCE_H__
#define __FIND_SUBLEDGERS_SEQUENCE_H__

#include "CommandSequence.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class FindSubledgersForAccountSequence : public CommandSequence<FindSubledgersForAccountSequence>
      {
      public:
         using currencyBalancePair = std::pair<std::string, double>;
         using onAccountBalanceLoaded = std::function< void(const std::vector<currencyBalancePair>& currencyBalances)>;
      public:
         FindSubledgersForAccountSequence(const std::shared_ptr<spdlog::logger>&
            , const std::string& accountName
            , const onAccountBalanceLoaded& cb);
         ~FindSubledgersForAccountSequence() = default;

         bool FinishSequence() override;

         CelerMessage sendFindSubledgersRequest();
         bool         processFindSubledgersResponse(const CelerMessage&);

      private:
         std::shared_ptr<spdlog::logger>  logger_;
         const onAccountBalanceLoaded     cb_;

         const std::string                   accountName_;
         std::vector<currencyBalancePair>    balancePairs_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __FIND_SUBLEDGERS_SEQUENCE_H__
