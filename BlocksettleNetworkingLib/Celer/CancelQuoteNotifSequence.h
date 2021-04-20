/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CELER_CANCEL_QUOTE_NOTIFICATION_SEQUENCE_H__
#define __CELER_CANCEL_QUOTE_NOTIFICATION_SEQUENCE_H__

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

      class CancelQuoteNotifSequence : public CommandSequence<CancelQuoteNotifSequence>
      {
      public:
         [[deprecated]] CancelQuoteNotifSequence(const QString &reqId, const QString &reqSessToken, const std::shared_ptr<spdlog::logger>& logger);
         CancelQuoteNotifSequence(const std::string& reqId, const std::string& reqSessToken
            , const std::shared_ptr<spdlog::logger>& logger);
         ~CancelQuoteNotifSequence() noexcept = default;

         CancelQuoteNotifSequence(const CancelQuoteNotifSequence&) = delete;
         CancelQuoteNotifSequence& operator = (const CancelQuoteNotifSequence&) = delete;
         CancelQuoteNotifSequence(CancelQuoteNotifSequence&&) = delete;
         CancelQuoteNotifSequence& operator = (CancelQuoteNotifSequence&&) = delete;

         bool FinishSequence() override;

      private:
         CelerMessage send();

         std::string    reqId_, reqSessToken_;
         size_t   colonPos_{ std::string::npos };
         std::shared_ptr<spdlog::logger> logger_;
      };

   }  //namespace celer
}  //namespace bs

#endif // __CELER_CANCEL_QUOTE_NOTIFICATION_SEQUENCE_H__
