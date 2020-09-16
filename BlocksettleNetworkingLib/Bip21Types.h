/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __BIP_21_TYPES_H__
#define __BIP_21_TYPES_H__

#include "XBTAmount.h"

#include <QDateTime>
#include <QString>

namespace Bip21
{
   struct PaymentRequestInfo
   {
      // address and amount are mandatory fields. all others could be empty
      QString        address;
      bs::XBTAmount  amount;
      QString        label;
      QString        message;
      QString        requestURL;
      float          feePerByte = 0;
      QDateTime      requestExpireDateTime;
      QString        requestMemo;
   };
};


#endif // __BIP_21_TYPES_H__
