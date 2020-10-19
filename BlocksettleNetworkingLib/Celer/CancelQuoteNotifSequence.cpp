/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
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
   , reqId_(reqId), reqSessToken_(reqSessToken)
   , logger_(logger)
{}


bool CancelQuoteNotifSequence::FinishSequence()
{
   return true;
}

CelerMessage CancelQuoteNotifSequence::send()
{
   QuoteCancelNotification request;

   request.set_quoterequestid(reqId_.toStdString());
   request.set_quotecanceltype(com::celertech::marketmerchant::api::enums::quotecanceltype::CANCEL_QUOTE_SPECIFIED_IN_QUOTEID);
   request.set_requestorsessionkey(reqSessToken_.section(QLatin1Char(':'), 0, 0).toStdString());
   request.set_requestorsessiontoken(reqSessToken_.toStdString());

   CelerMessage message;
   message.messageType = CelerAPI::QuoteCancelNotificationType;
   message.messageData = request.SerializeAsString();

   logger_->debug("[CelerCancelQuoteNotifSequence::send] {}", request.DebugString());

   return message;
}
