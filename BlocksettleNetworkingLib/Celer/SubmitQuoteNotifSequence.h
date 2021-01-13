/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_SUBMIT_QUOTE_NOTIF_H__
#define __CELER_SUBMIT_QUOTE_NOTIF_H__

#include "CommandSequence.h"
#include "CommonTypes.h"
#include <string>
#include <functional>
#include <memory>

namespace spdlog
{
   class logger;
}

namespace bs {
   namespace celer {

      class SubmitQuoteNotifSequence : public CommandSequence<SubmitQuoteNotifSequence>
      {
      public:
         SubmitQuoteNotifSequence(const std::string& accountName
            , const bs::network::QuoteNotification &, const std::shared_ptr<spdlog::logger>&);
         ~SubmitQuoteNotifSequence() noexcept = default;

         SubmitQuoteNotifSequence(const SubmitQuoteNotifSequence&) = delete;
         SubmitQuoteNotifSequence& operator = (const SubmitQuoteNotifSequence&) = delete;
         SubmitQuoteNotifSequence(SubmitQuoteNotifSequence&&) = delete;
         SubmitQuoteNotifSequence& operator = (SubmitQuoteNotifSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage submitQuoteNotif();

         const std::string accountName_;
         bs::network::QuoteNotification   qn_;
         std::shared_ptr<spdlog::logger>  logger_;
      };
   }  //namespace celer
}  //namespace bs


#endif // __CELER_SUBMIT_RFQ_H__
