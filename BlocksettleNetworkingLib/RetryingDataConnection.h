/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef RETRYING_DATA_CONNECTION_H
#define RETRYING_DATA_CONNECTION_H

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "DataConnection.h"

class DispatchQueue;

struct RetryingDataConnectionParams
{
   std::chrono::milliseconds restartTime{std::chrono::seconds(10)};
   std::chrono::milliseconds periodicCheckTime{std::chrono::seconds(20)};
   std::unique_ptr<DataConnection> connection;
};

namespace spdlog {
   class logger;
}
class RetryingDataConnectionListener;

class RetryingDataConnection : public DataConnection
{
public:
   RetryingDataConnection(const std::shared_ptr<spdlog::logger>& logger, RetryingDataConnectionParams params);
   ~RetryingDataConnection() override;

   RetryingDataConnection(const RetryingDataConnection&) = delete;
   RetryingDataConnection& operator = (const RetryingDataConnection&) = delete;
   RetryingDataConnection(RetryingDataConnection&&) = delete;
   RetryingDataConnection& operator = (RetryingDataConnection&&) = delete;

   bool send(const std::string &data) override;

   bool openConnection(const std::string &host, const std::string &port, DataConnectionListener *listener) override;
   bool closeConnection() override;

   bool isActive() const override;

private:
   friend class RetryingDataConnectionListener;

   enum State
   {
      Idle,
      Connecting,
      Connected,
      WaitingRestart,
   };

   // Could be used from listening thread only
   void trySendPackets();
   void tryReconnectIfNeeded();
   void scheduleRestart();
   void restart();

   void onConnected();
   void onDisconnected();
   void onError(DataConnectionListener::DataConnectionError errorCode);

   std::shared_ptr<spdlog::logger> logger_;
   const RetryingDataConnectionParams params_;

   std::unique_ptr<DispatchQueue> queue_;
   std::thread thread_;

   // Could be used from listening thread only
   std::queue<std::string> packets_;
   State state_{State::Idle};
   std::chrono::steady_clock::time_point restartTime_{};
   std::string host_;
   std::string port_{};
   std::unique_ptr<RetryingDataConnectionListener> ownListener_;
   std::atomic_bool shuttingDown_{false};

};

#endif // RETRYING_DATA_CONNECTION_H
