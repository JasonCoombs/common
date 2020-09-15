/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BitPayRequests.h"

#include <spdlog/spdlog.h>

namespace BitPay {

QNetworkRequest getPaymentOptionsRequest(const QString& url)
{
   QNetworkRequest request;
   request.setUrl(QUrl(url));
   request.setRawHeader("Accept", "application/payment-options");
   request.setRawHeader("x-paypro-version", "2");

   return request;
}

QNetworkRequest getBTCPaymentRequest(const QString& url)
{
   QNetworkRequest request;
   request.setUrl(QUrl(url));
   request.setRawHeader("Content-Type", "application/payment-request");
   request.setRawHeader("x-paypro-version", "2");

   return request;
}

QByteArray getBTCPaymentRequestPayload()
{
   return QByteArray::fromStdString("{\"chain\":\"BTC\"}");
}

QNetworkRequest getBTCPaymentVerificationRequest(const QString& url)
{
   QNetworkRequest request;
   request.setUrl(QUrl(url));
   request.setRawHeader("Content-Type", "application/payment-verification");
   request.setRawHeader("x-paypro-version", "2");

   return request;
}

QByteArray getBTCPaymentVerificationPayload(const std::string& serializedHexTx, uint64_t weightedSize)
{
   const auto payloadStr = fmt::format("{\"chain\":\"BTC\",\"transactions\":[{\"tx\":\"{}\",\"weightedSize\":{}}]}", serializedHexTx, weightedSize);
   return QByteArray::fromStdString(payloadStr);
}

};
