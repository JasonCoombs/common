/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __UTXO_RESERVATION_H__
#define __UTXO_RESERVATION_H__

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include "TxClasses.h"

namespace spdlog {
   class logger;
}

namespace bs {
   // A reservation system for UTXOs. It can be fed a list of inputs. The inputs
   // are then set aside and made unavailable for future usage. This is useful
   // for keeping UTXOs from being used, and for accessing UTXOs later (e.g.,
   // when zero conf TXs arrive and the inputs need to be accessed quickly).
   class UtxoReservation
   {
   public:
      explicit UtxoReservation(const std::shared_ptr<spdlog::logger>& logger);
      // Create the singleton. Use only once!
      // Destroying disabled as it's broken, see BST-2362 for details
      [[deprecated]] static void init(const std::shared_ptr<spdlog::logger> &logger);

      // Reserve/Unreserve UTXOs. Used as needed. User supplies the wallet ID,
      // a reservation ID, and the UTXOs to reserve.
      bool reserve(const std::string& reserveId, const std::vector<UTXO>& utxos
         , const std::string& subId = {});
      bool unreserve(const std::string &reserveId, const std::string& subId = {});

      // Get the UTXOs based on the reservation ID.
      [[nodiscard]] std::vector<UTXO> get(const std::string &reserveId
         , const std::string& subId = {}) const;

      // Pass in a vector of UTXOs. If any of the UTXOs are in the wallet ID
      // being queried, remove the UTXOs from the vector.
      size_t filter(std::vector<UTXO> &utxos, std::vector<UTXO> &filtered) const;

      bool containsReservedUTXO(const std::vector<UTXO> &utxos) const;

      size_t cleanUpReservations(const std::chrono::seconds &);

      // Check that all reservations have been cleared
      void shutdownCheck();

      [[deprecated]] static UtxoReservation *instance();

   private:
      using UTXOs = std::vector<UTXO>;
      using UTXOMap = std::unordered_map<std::string, UTXOs>;
      using IdList = std::unordered_set<std::string>;

      mutable std::mutex mutex_;

      // Reservation ID, UTXO vector.
      std::unordered_map<std::string, UTXOMap> byReserveId_;

      // Reservation ID, time of reservation
      std::unordered_map<std::string, std::unordered_map<std::string
         , std::chrono::steady_clock::time_point>> reserveTime_;

      std::set<UTXO> reserved_;

      std::shared_ptr<spdlog::logger> logger_;
   };

}  //namespace bs

#endif //__UTXO_RESERVATION_H__
