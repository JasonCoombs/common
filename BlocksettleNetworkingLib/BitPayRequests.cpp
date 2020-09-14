/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "BitPayRequests.h"

namespace BitPay {

QNetworkRequest getPaymentOptionsRequest(const QString& url)
{
   QNetworkRequest request;
   request.setUrl(QUrl(url));
   request.setRawHeader("Accept", "application/payment-options");
   request.setRawHeader("x-paypro-version", "2");

   return request;
}

QNetworkRequest getPaymentRequest(const QString& url)
{
   QNetworkRequest request;
   request.setUrl(QUrl(url));
   request.setRawHeader("Content-Type", "application/payment-options");
   request.setRawHeader("x-paypro-version", "2");

   return request;
}

QByteArray getBTCPaymentRequestPayload()
{
   return QByteArray::fromStdString("{\"chain\":\"BTC\"}");
}
};
