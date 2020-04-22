/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "UtxoReservation.h"

#include <thread>
#include <spdlog/spdlog.h>

using namespace bs;


// Global UTXO reservation singleton.
static std::shared_ptr<bs::UtxoReservation> utxoResInstance_;

bs::UtxoReservation::UtxoReservation(const std::shared_ptr<spdlog::logger> &logger)
   : logger_(logger)
{
}

// Singleton reservation.
void bs::UtxoReservation::init(const std::shared_ptr<spdlog::logger> &logger)
{
   assert(!utxoResInstance_);
   utxoResInstance_ = std::shared_ptr<bs::UtxoReservation>(new bs::UtxoReservation(logger));
}

void UtxoReservation::shutdownCheck()
{
   std::lock_guard<std::mutex> lock(mutex_);
   for (const auto &reserveIdItem : byReserveId_) {
      auto reserveTime = std::chrono::steady_clock::now() - reserveTime_[reserveIdItem.first];
      SPDLOG_LOGGER_ERROR(logger_, "UTXO reservation was not cleared: {}, reserved {} seconds ago"
         , reserveIdItem.first, reserveTime / std::chrono::seconds(1));
   }
}

// Reserve a set of UTXOs for a wallet and reservation ID. Reserve across all
// active adapters.
void bs::UtxoReservation::reserve(const std::string &reserveId
   , const std::vector<UTXO> &utxos)
{
   const auto curTime = std::chrono::steady_clock::now();
   std::lock_guard<std::mutex> lock(mutex_);

   auto it = byReserveId_.find(reserveId);
   if (it != byReserveId_.end()) {
      SPDLOG_LOGGER_ERROR(logger_, "reservation '{}' already exist", reserveId);
      return;
   }

   byReserveId_[reserveId] = utxos;
   reserveTime_[reserveId] = curTime;

   for (const auto &utxo : utxos) {
      auto result = reserved_.insert(utxo);
      if (!result.second) {
         SPDLOG_LOGGER_ERROR(logger_, "found duplicated reserved UTXO!");
      }
   }
}

// Unreserve a set of UTXOs for a wallet and reservation ID. Return the
// associated wallet ID. Unreserve across all active adapters.
bool bs::UtxoReservation::unreserve(const std::string &reserveId)
{
   std::lock_guard<std::mutex> lock(mutex_);
   const auto it = byReserveId_.find(reserveId);
   if (it == byReserveId_.end()) {
      return false;
   }

   for (const auto &utxo : it->second) {
      reserved_.erase(utxo);
   }

   byReserveId_.erase(it);
   reserveTime_.erase(reserveId);

   return true;
}

// Get UTXOs for a given reservation ID.
std::vector<UTXO> bs::UtxoReservation::get(const std::string &reserveId) const
{
   std::lock_guard<std::mutex> lock(mutex_);

   const auto it = byReserveId_.find(reserveId);
   if (it == byReserveId_.end()) {
      return {};
   }
   return it->second;
}

// For a given wallet ID, filter out all associated UTXOs from a list of UTXOs.
// True if success, false if failure.
void bs::UtxoReservation::filter(std::vector<UTXO> &utxos, std::vector<UTXO> &filtered) const
{
   filtered.clear();
   std::lock_guard<std::mutex> lock(mutex_);

   auto it = utxos.begin();
   while (it != utxos.end()) {
      if (reserved_.find(*it) != reserved_.end()) {
         filtered.push_back(*it);
         it = utxos.erase(it);
      } else {
         ++it;
      }
   }
}

bool bs::UtxoReservation::containsReservedUTXO(const std::vector<UTXO> &utxos) const
{
   std::lock_guard<std::mutex> lock(mutex_);

   for ( auto it = utxos.begin(); it != utxos.end(); ++it) {
      if (reserved_.find(*it) != reserved_.end()) {
         return true;
      }
   }

   return false;
}

UtxoReservation *UtxoReservation::instance()
{
   return utxoResInstance_.get();
}
