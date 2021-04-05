/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "SettlementMonitor.h"

#include "CheckRecipSigner.h"
#include "CoinSelection.h"
#include "FastLock.h"
#include "TradesVerification.h"
#include "Wallets/SyncWallet.h"

std::string bs::SettlementMonitor::PayinStateToString(const PayinState &state)
{
   static const std::string stateUnknown = "Unknown";
   static const std::string stateUnconfirmed = "Unconfirmed";
   static const std::string stateConfirmed = "Confirmed";
   static const std::string stateDoubleSpend = "DoubleSpend";
   static const std::string stateSuspicious = "Suspicious";
   static const std::string stateBroadcastFailed = "BroadcastFailed";

   switch (state) {
   case bs::SettlementMonitor::PayinState::Unknown: return stateUnknown;
   case bs::SettlementMonitor::PayinState::Unconfirmed: return stateUnconfirmed;
   case bs::SettlementMonitor::PayinState::Confirmed: return stateConfirmed;
   case bs::SettlementMonitor::PayinState::DoubleSpend: return stateDoubleSpend;
   case bs::SettlementMonitor::PayinState::Suspicious: return stateSuspicious;
   case bs::SettlementMonitor::PayinState::BroadcastFailed: return stateBroadcastFailed;
   default:
      return std::string("Unexpected payin state ") + std::to_string(static_cast<int>(state));
   }
}

std::string bs::SettlementMonitor::PayoutStateToString(const PayoutState& state)
{
   static const std::string stateUnknown = "Unknown";
   static const std::string stateUnconfirmed = "Unconfirmed";
   static const std::string stateConfirmed = "Confirmed";
   static const std::string stateDoubleSpend = "DoubleSpend";
   static const std::string stateRevoked = "Revoked";
   static const std::string stateSuspicious = "Suspicious";

   switch (state) {
   case bs::SettlementMonitor::PayoutState::Unknown: return stateUnknown;
   case bs::SettlementMonitor::PayoutState::Unconfirmed: return stateUnconfirmed;
   case bs::SettlementMonitor::PayoutState::Confirmed: return stateConfirmed;
   case bs::SettlementMonitor::PayoutState::DoubleSpend: return stateDoubleSpend;
   case bs::SettlementMonitor::PayoutState::Revoked: return stateRevoked;
   case bs::SettlementMonitor::PayoutState::Suspicious: return stateSuspicious;
   default:
      return std::string("Unexpected payout state ") + std::to_string(static_cast<int>(state));
   }
}