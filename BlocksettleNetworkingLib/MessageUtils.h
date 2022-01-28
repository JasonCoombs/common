/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef MESSAGE_UTILS_H
#define MESSAGE_UTILS_H

#include "Message/Bus.h"
#include "Message/Envelope.h"
#include "CommonTypes.h"

#include "bs_types.pb.h"

namespace BlockSettle {
   namespace Terminal {
      class RFQ;
      class Quote;
      class IncomingRFQ;
      class MatchingMessage_Order;
      class ReplyToRFQ;
   }
}

namespace bs {
/*   namespace types {
      enum Side;  //Fwd-decl doesn't work for gcc for some reason
   }*/

   namespace message {

      network::Side::Type fromBS(const types::Side& side);
      types::Side toBS(const network::Side::Type& side);

      bs::network::RFQ fromMsg(const BlockSettle::Terminal::RFQ&);
      void toMsg(const bs::network::RFQ&, BlockSettle::Terminal::RFQ*);

      bs::network::Order fromMsg(const BlockSettle::Terminal::MatchingMessage_Order&);
      void toMsg(const bs::network::Order&, BlockSettle::Terminal::MatchingMessage_Order*);

      bs::network::Quote fromMsg(const BlockSettle::Terminal::Quote&);
      void toMsg(const bs::network::Quote&, BlockSettle::Terminal::Quote*);

      bs::network::QuoteReqNotification fromMsg(const BlockSettle::Terminal::IncomingRFQ&);
      void toMsg(const bs::network::QuoteReqNotification&
         , BlockSettle::Terminal::IncomingRFQ*);

      bs::network::QuoteNotification fromMsg(const BlockSettle::Terminal::ReplyToRFQ&);
      void toMsg(const bs::network::QuoteNotification&
         , BlockSettle::Terminal::ReplyToRFQ*);
   } // namespace message
} // namespace bs

#endif	// MESSAGE_UTILS_H
