/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
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
{}

// Singleton reservation.
void bs::UtxoReservation::init(const std::shared_ptr<spdlog::logger> &logger)
{
   assert(!utxoResInstance_);
   utxoResInstance_ = std::make_shared<bs::UtxoReservation>(logger);
}

void UtxoReservation::shutdownCheck()
{
   std::lock_guard<std::mutex> lock(mutex_);
   for (const auto &reserveItem : byReserveId_) {
      for (const auto& resData : reserveItem.second) {
         const auto& reserveTime = std::chrono::steady_clock::now() -
            reserveTime_[reserveItem.first][resData.first];
         SPDLOG_LOGGER_ERROR(logger_, "UTXO reservation was not cleared: {}/{},"
            " reserved {} seconds ago", reserveItem.first, resData.first
            , reserveTime / std::chrono::seconds(1));
      }
   }
}

// Reserve a set of UTXOs for a wallet and reservation ID. Reserve across all
// active adapters.
bool bs::UtxoReservation::reserve(const std::string &reserveId
   , const std::vector<UTXO> &utxos, const std::string& subId)
{
   const auto &curTime = std::chrono::steady_clock::now();
   std::lock_guard<std::mutex> lock(mutex_);

   const auto &it = byReserveId_.find(reserveId);
   if (it != byReserveId_.end()) {
      const auto& itSub = it->second.find(subId);
      if (itSub != it->second.end()) {
         SPDLOG_LOGGER_ERROR(logger_, "reservation {}/{} already exists"
            , reserveId, subId);
         return false;
      }
   }

   for (const auto &utxo : utxos) {
      const auto &result = reserved_.insert(utxo);
      if (!result.second) {   //TODO: probably we should return false here
         SPDLOG_LOGGER_WARN(logger_, "found duplicated reserved UTXO {}/{}"
            , utxo.getTxHash().toHexStr(true), utxo.getTxOutIndex());
      }
   }

   byReserveId_[reserveId][subId] = utxos;
   reserveTime_[reserveId][subId] = curTime;
   return true;
}

// Unreserve a set of UTXOs for a wallet and reservation ID. Return the
// associated wallet ID. Unreserve across all active adapters.
bool bs::UtxoReservation::unreserve(const std::string &reserveId, const std::string& subId)
{
   std::lock_guard<std::mutex> lock(mutex_);

   const auto &it = byReserveId_.find(reserveId);
   if (it == byReserveId_.end()) {
      return false;
   }
   if (subId.empty()) {
      reserveTime_.erase(reserveId);
      for (const auto& sub : it->second) {
         for (const auto& utxo : sub.second) {
            reserved_.erase(utxo);
         }
      }
      byReserveId_.erase(it);
   }
   else {
      const auto& itSub = it->second.find(subId);
      if (itSub == it->second.end()) {
         return false;
      }
      reserveTime_[reserveId].erase(itSub->first);
      try {
         if (reserveTime_.at(reserveId).empty()) {
            reserveTime_.erase(reserveId);
         }
      }
      catch (const std::exception&) {}

      for (const auto& utxo : itSub->second) {
         reserved_.erase(utxo);
      }

      it->second.erase(itSub);
      if (it->second.empty()) {
         byReserveId_.erase(it);
      }
   }
   return true;
}

// Get UTXOs for a given reservation ID.
std::vector<UTXO> bs::UtxoReservation::get(const std::string &reserveId
   , const std::string &subId) const
{
   std::lock_guard<std::mutex> lock(mutex_);

   const auto &it = byReserveId_.find(reserveId);
   if (it == byReserveId_.end()) {
      return {};
   }
   const auto& itSub = it->second.find(subId);
   if (itSub == it->second.end()) {
      if (subId.empty()) {
         std::vector<UTXO> result;
         for (const auto utxos : it->second) {
            result.insert(result.cend(), utxos.second.cbegin(), utxos.second.cend());
         }
         return result;
      }
      else {
         return {};
      }
   }
   return itSub->second;
}

std::vector<std::string> bs::UtxoReservation::getSubIds(const std::string& reserveId)
{
   std::vector<std::string> result;
   std::lock_guard<std::mutex> lock(mutex_);
   const auto& itReserve = byReserveId_.find(reserveId);
   if (itReserve != byReserveId_.end()) {
      for (const auto& subId : itReserve->second) {
         result.push_back(subId.first);
      }
   }
   return result;
}

// For a given wallet ID, filter out all associated UTXOs from a list of UTXOs.
// Returns the number of filtered/removed entries.
size_t bs::UtxoReservation::filter(std::vector<UTXO> &utxos, std::vector<UTXO> &filtered) const
{
   filtered.clear();
   if (utxos.empty()) {
      return 0;
   }
   size_t nbFiltered = 0;
   std::lock_guard<std::mutex> lock(mutex_);

   auto it = utxos.begin();
   while (it != utxos.end()) {
      if ((reserved_.find(*it) != reserved_.end()) || !it->isInitialized()) {
         filtered.push_back(*it);
         it = utxos.erase(it);
         nbFiltered++;
      } else {
         ++it;
      }
   }
   return nbFiltered;
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

size_t bs::UtxoReservation::cleanUpReservations(const std::chrono::seconds& interval)
{
   std::vector<std::pair<std::string, std::string>> reservationsToDelete;
   const auto& timeNow = std::chrono::steady_clock::now();
   for (const auto& resTMap : reserveTime_) {
      for (const auto& resTime : resTMap.second) {
         if ((timeNow - resTime.second) > interval) {
            reservationsToDelete.push_back({ resTMap.first, resTime.first });
         }
      }
   }
   size_t cleanedUp = 0;
   for (const auto& resId : reservationsToDelete) {
      if (unreserve(resId.first, resId.second)) {
         cleanedUp++;
      }
   }
   return cleanedUp;
}

UtxoReservation *UtxoReservation::instance()
{
   return utxoResInstance_.get();
}
