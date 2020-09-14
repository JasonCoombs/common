/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __BITPAY_REQUESTS_H__
#define __BITPAY_REQUESTS_H__

#include <QByteArray>
#include <QNetworkRequest>
#include <QString>

namespace BitPay
{
   QNetworkRequest getPaymentOptionsRequest(const QString& url);

   QNetworkRequest   getBTCPaymentRequest(const QString& url);
   QByteArray        getBTCPaymentRequestPayload();

};

#endif // __BITPAY_REQUESTS_H__
