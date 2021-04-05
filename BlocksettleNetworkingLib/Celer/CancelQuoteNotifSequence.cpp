/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CancelQuoteNotifSequence.h"
#include "UpstreamQuoteProto.pb.h"
#include <QDate>
#include <spdlog/spdlog.h>

using namespace bs::celer;
using namespace com::celertech::marketmerchant::api::quote;

CancelQuoteNotifSequence::CancelQuoteNotifSequence(const QString &reqId
   , const QString &reqSessToken, const std::shared_ptr<spdlog::logger>& logger)
   : CommandSequence("CelerCancelQuoteNotifSequence", {
         { false, nullptr, &CancelQuoteNotifSequence::send }
      })
   , reqId_(reqId.toStdString()), reqSessToken_(reqSessToken.toStdString())
   , logger_(logger), colonPos_(reqSessToken.toStdString().find(':'))
{}

CancelQuoteNotifSequence::CancelQuoteNotifSequence(const std::string& reqId
   , const std::string& reqSessToken, const std::shared_ptr<spdlog::logger>& logger)
   : CommandSequence("CelerCancelQuoteNotifSequence", {
         { false, nullptr, &CancelQuoteNotifSequence::send }
      })
   , reqId_(reqId), reqSessToken_(reqSessToken)
   , logger_(logger)
{
   if (!reqSessToken_.empty()) {
      colonPos_ = reqSessToken_.find(':');
      if (colonPos_ == std::string::npos) {
         throw std::invalid_argument("session token doesn't contain colon");
      } else if (colonPos_ == 0) {
         throw std::invalid_argument("session token has empty session key");
      }
   }
}


bool CancelQuoteNotifSequence::FinishSequence()
{
   return true;
}

CelerMessage CancelQuoteNotifSequence::send()
{
   QuoteCancelNotification request;

   request.set_quoterequestid(reqId_);
   request.set_quotecanceltype(com::celertech::marketmerchant::api::enums::quotecanceltype::CANCEL_QUOTE_SPECIFIED_IN_QUOTEID);

   if (reqSessToken_.empty()) {
      request.set_requestorsessionkey("");
      request.set_requestorsessiontoken("");
   }
   else {
      request.set_requestorsessionkey(reqSessToken_.substr(0, colonPos_ - 1));
      request.set_requestorsessiontoken(reqSessToken_);
   }

   CelerMessage message;
   message.messageType = CelerAPI::QuoteCancelNotificationType;
   message.messageData = request.SerializeAsString();

   logger_->debug("[CelerCancelQuoteNotifSequence::send] {}", request.DebugString());
   return message;
}
