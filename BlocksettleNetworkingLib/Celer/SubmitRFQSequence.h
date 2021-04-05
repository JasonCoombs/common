/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_SUBMIT_RFQ_H__
#define __CELER_SUBMIT_RFQ_H__

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
      class SubmitRFQSequence : public CommandSequence<SubmitRFQSequence>
      {
      public:
         SubmitRFQSequence(const std::string& accountName, const bs::network::RFQ& rfq
            , const std::shared_ptr<spdlog::logger>&, bool debugPrintRFQ = true);
         ~SubmitRFQSequence() noexcept = default;

         SubmitRFQSequence(const SubmitRFQSequence&) = delete;
         SubmitRFQSequence& operator = (const SubmitRFQSequence&) = delete;

         SubmitRFQSequence(SubmitRFQSequence&&) = delete;
         SubmitRFQSequence& operator = (SubmitRFQSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage submitRFQ();

         const std::string accountName_;
         bs::network::RFQ  rfq_;
         std::shared_ptr<spdlog::logger> logger_;

         bool debugPrintRFQ_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_SUBMIT_RFQ_H__
