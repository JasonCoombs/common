/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SignTxSequence.h"

#include "bitcoin/UpstreamBitcoinTransactionSigningProto.pb.h"

#include <QDate>

#include <spdlog/spdlog.h>

using namespace bs::celer;
using namespace com::celertech::marketmerchant::api::order::bitcoin;

SignTxSequence::SignTxSequence(const QString &orderId, const std::string &txData
   , const std::shared_ptr<spdlog::logger>& logger)
 : CommandSequence("CelerSignTxSequence", {
         { false, nullptr, &SignTxSequence::send }
      })
   , orderId_(orderId)
   , txData_(txData)
   , logger_(logger)
{}

CelerMessage SignTxSequence::send()
{
   SignTransactionRequest request;

   request.set_orderid(orderId_.toStdString());
   request.set_signedtransaction(txData_);

   CelerMessage message;
   message.messageType = CelerAPI::SignTransactionRequestType;
   message.messageData = request.SerializeAsString();

   logger_->debug("SignTransaction: {}", request.DebugString());

   return message;
}
