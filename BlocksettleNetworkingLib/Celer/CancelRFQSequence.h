/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_CANCEL_RFQ_SEQUENCE_H__
#define __CELER_CANCEL_RFQ_SEQUENCE_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <string>
#include <functional>
#include <memory>

namespace spdlog {
   class logger;
}

namespace bs {
   namespace celer {

      class CancelRFQSequence : public CommandSequence<CancelRFQSequence>
      {
      public:
         CancelRFQSequence(const QString &reqId, const std::shared_ptr<spdlog::logger>& logger);
         ~CancelRFQSequence() noexcept override = default;

         CancelRFQSequence(const CancelRFQSequence&) = delete;
         CancelRFQSequence& operator = (const CancelRFQSequence&) = delete;
         CancelRFQSequence(CancelRFQSequence&&) = delete;
         CancelRFQSequence& operator = (CancelRFQSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage cancelRFQ();

         QString  reqId_;
         std::shared_ptr<spdlog::logger> logger_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_CANCEL_RFQ_SEQUENCE_H__
