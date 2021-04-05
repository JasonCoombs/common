/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "RetryingDataConnection.h"

#include "DispatchQueue.h"

#include <spdlog/spdlog.h>

class RetryingDataConnectionListener : public DataConnectionListener
{
public:
   void OnDataReceived(const std::string& data) override
   {
      owner_->listener_->OnDataReceived(data);
   }

   void OnConnected() override
   {
      owner_->queue_->dispatch([this] {
         owner_->onConnected();
      });
   }

   void OnDisconnected() override
   {
      owner_->queue_->dispatch([this] {
         owner_->onDisconnected();
      });
   }

   void OnError(DataConnectionError errorCode) override
   {
      owner_->queue_->dispatch([this, errorCode] {
         owner_->onError(errorCode);
      });
   }

   RetryingDataConnection *owner_{};
};

RetryingDataConnection::RetryingDataConnection(const std::shared_ptr<spdlog::logger>& logger
   , RetryingDataConnectionParams params)
   : logger_(logger)
   , params_(std::move(params))
{
   ownListener_ = std::make_unique<RetryingDataConnectionListener>();
   ownListener_->owner_ = this;
}

RetryingDataConnection::~RetryingDataConnection()
{
   shuttingDown_ = true;
   closeConnection();
}

bool RetryingDataConnection::send(const std::string &data)
{
   queue_->dispatch([this, data] {
      packets_.push(data);
      trySendPackets();
   });
   return true;
}

bool RetryingDataConnection::openConnection(const std::string &host, const std::string &port
   , DataConnectionListener *listener)
{
   closeConnection();

   queue_ = std::make_unique<DispatchQueue>();

   thread_ = std::thread([this] {
      while (!queue_->done()) {
         queue_->tryProcess(params_.periodicCheckTime);
         tryReconnectIfNeeded();
      }
   });

   queue_->dispatch([this, host, port, listener] {
      host_ = host;
      port_ = port;
      listener_ = listener;
      restart();
   });
   return true;
}

bool RetryingDataConnection::closeConnection()
{
   if (!isActive()) {
      return false;
   }

   queue_->dispatch([this] {
      params_.connection->closeConnection();
   });
   queue_->quit();
   thread_.join();
   state_ = State::Idle;
   return true;
}

bool RetryingDataConnection::isActive() const
{
   return thread_.joinable();
}

void RetryingDataConnection::trySendPackets()
{
   while (state_ == State::Connected && !packets_.empty()) {
      bool result = params_.connection->send(packets_.front());
      if (!result) {
         SPDLOG_LOGGER_ERROR(logger_, "sending packet failed");
         scheduleRestart();
         return;
      }
      packets_.pop();
   }
}

void RetryingDataConnection::tryReconnectIfNeeded()
{
   if (state_ == State::WaitingRestart && std::chrono::steady_clock::now() >= restartTime_ && !shuttingDown_) {
      restart();
   }
}

void RetryingDataConnection::scheduleRestart()
{
   state_ = State::WaitingRestart;
   restartTime_ = std::chrono::steady_clock::now() + params_.restartTime;
}

void RetryingDataConnection::restart()
{
   bool result = params_.connection->openConnection(host_, port_, ownListener_.get());
   if (!result) {
      SPDLOG_LOGGER_ERROR(logger_, "opening connection failed");
      scheduleRestart();
      return;
   }
   state_ = State::Connecting;
}

void RetryingDataConnection::onConnected()
{
   listener_->OnConnected();
   state_ = State::Connected;
   trySendPackets();
}

void RetryingDataConnection::onDisconnected()
{
   listener_->OnDisconnected();
   scheduleRestart();
}

void RetryingDataConnection::onError(DataConnectionListener::DataConnectionError errorCode)
{
   listener_->OnError(errorCode);
   scheduleRestart();
}
