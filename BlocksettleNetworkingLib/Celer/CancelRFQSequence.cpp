/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "CancelRFQSequence.h"

#include "UpstreamQuoteProto.pb.h"

#include <QDate>

#include <spdlog/spdlog.h>

using namespace bs::celer;
using namespace com::celertech::marketmerchant::api::quote;

CancelRFQSequence::CancelRFQSequence(const QString &reqId, const std::shared_ptr<spdlog::logger>& logger)
 : CommandSequence("CelerCancelRFQSequence", {
         { false, nullptr, &CancelRFQSequence::cancelRFQ }
      })
   , reqId_(reqId)
   , logger_(logger)
{}


bool CancelRFQSequence::FinishSequence()
{
   return true;
}

CelerMessage CancelRFQSequence::cancelRFQ()
{
   QuoteCancelRequest request;

   request.set_quoterequestid(reqId_.toStdString());
   request.set_quotecanceltype(com::celertech::marketmerchant::api::enums::quotecanceltype::CANCEL_QUOTE_SPECIFIED_IN_QUOTEID);

   CelerMessage message;
   message.messageType = CelerAPI::QuoteCancelRequestType;
   message.messageData = request.SerializeAsString();

   logger_->debug("CancelRFQ: {}", request.DebugString());

   return message;
}
