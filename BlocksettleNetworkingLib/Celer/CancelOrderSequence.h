/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_CANCEL_ORDER_H__
#define __CELER_CANCEL_ORDER_H__

#include "CommandSequence.h"
#include "CommonTypes.h"

#include <memory>
#include <string>
#include <functional>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {
      class CancelOrderSequence : public CommandSequence<CancelOrderSequence>
      {
      public:
         CancelOrderSequence(int64_t orderId, const std::string& clientOrderId, const std::shared_ptr<spdlog::logger>& logger);
         ~CancelOrderSequence() noexcept = default;

         CancelOrderSequence(const CancelOrderSequence&) = delete;
         CancelOrderSequence& operator = (const CancelOrderSequence&) = delete;

         CancelOrderSequence(CancelOrderSequence&&) = delete;
         CancelOrderSequence& operator = (CancelOrderSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage cancelOrder();

         int64_t     orderId_;
         std::string clientOrderId_;
         std::shared_ptr<spdlog::logger> logger_;
      };

   }  // namespace celer
}  // namespace bs

#endif // __CELER_CANCEL_ORDER_H__
