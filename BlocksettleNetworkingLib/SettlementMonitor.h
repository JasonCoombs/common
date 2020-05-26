/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __SETTLEMENT_MONITOR_H__
#define __SETTLEMENT_MONITOR_H__

#include "ArmoryConnection.h"
#include "CoreWallet.h"
#include "TradesVerification.h"
#include "ValidityFlag.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>


namespace bs {

   class SettlementMonitor
   {
   public:
      enum class PayinState {
         Unknown, //not processed yet
         Unconfirmed, //in the mempool
         Confirmed, //conf > 0
         DoubleSpend, //at least one supporting utxo is spent by another tx with conf > 0
         Suspicious, //bad state that can't be handled automatically, requires human review
         BroadcastFailed //failed to push zc to the mempool
      };
      static std::string PayinStateToString(const PayinState &state);

      enum class PayoutState {
         Unknown, //not processed yet
         Unconfirmed, //in the mempool
         Confirmed, //conf > 0
         DoubleSpend, //settlement utxo is spent by another tx (that is not the revoke) with conf > 0
         Revoked, //seller's revoke tx has conf > 0
         Suspicious //bad state that can't be handled automatically, requires human review
      };
      static std::string PayoutStateToString(const PayoutState& state);
   };

} //namespace bs

#endif // __SETTLEMENT_MONITOR_H__
