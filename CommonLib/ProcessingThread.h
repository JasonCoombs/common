/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#ifndef __PROCESSING_THREAD_H__
#define __PROCESSING_THREAD_H__

#include <atomic>
#include <chrono>
#include <memory>
#include <queue>
#include <thread>
#include <type_traits>

#include "FastLock.h"
#include "ManualResetEvent.h"

template <typename T>
class ProcessingThread
{
   static_assert(std::is_copy_constructible<T>::value, "ProcessingThread packet type should be copy constructable");
public:
   ProcessingThread()
   {
      processingThread_ = std::thread(&ProcessingThread<T>::processingLoop, this);

   }

   virtual ~ProcessingThread() noexcept
   {
      haltProcessing();
      continueExecution_ = false;
      pendingPacketsEvent_.SetEvent();
      if (processingThread_.joinable()) {
         processingThread_.join();
      }
   }

   ProcessingThread(const ProcessingThread&) = delete;
   ProcessingThread& operator = (const ProcessingThread&) = delete;

   ProcessingThread(ProcessingThread&&) = delete;
   ProcessingThread& operator = (ProcessingThread&&) = delete;

   void SchedulePacketProcessing(const T& packet, const std::chrono::milliseconds& delay = {})
   {
      if (processingHalted_) {
         return;
      }
      const auto& p = std::make_shared<T>(packet);
      if (delay.count() == 0) {
         FastLock locker{ pendingPacketsLock_ };
         pendingPackets_.emplace(p);
         pendingPacketsEvent_.SetEvent();
      }
      else {
         const auto& execTime = std::chrono::steady_clock::now() + delay;
         FastLock locker{ pendingPacketsLock_ };
         delayedPackets_[execTime].push_back(p);
      }
   }

   virtual void processPacket(const T& packet) = 0;

   void haltProcessing()
   {
      processingHalted_ = true;
      cleanQueue();
   }

   void continueProcessing()
   {
      processingHalted_ = false;
   }

private:
   void cleanQueue()
   {
      FastLock locker{pendingPacketsLock_};
      decltype(pendingPackets_) cleanQueue;
      pendingPackets_.swap(cleanQueue);
   }

private:
   std::map<std::chrono::steady_clock::time_point, std::vector<std::shared_ptr<T>>> delayedPackets_;

   void processingLoop()
   {
      std::vector<std::shared_ptr<T>> packets;

      while (continueExecution_) {
         const bool expired = !pendingPacketsEvent_.WaitForEvent(std::chrono::milliseconds{ 50 });
         if (!continueExecution_) {
            break;
         }

         typename decltype(delayedPackets_)::const_iterator itDelayed;

         {
            FastLock locker{ pendingPacketsLock_ };
            if (expired) {
               const auto& timeNow = std::chrono::steady_clock::now();
               while ((itDelayed = delayedPackets_.cbegin()) != delayedPackets_.end()) {
                  if (itDelayed->first > timeNow) {
                     break;
                  }
                  for (const auto& packet : itDelayed->second) {
                     if (packet == nullptr) {
                        continue;
                     }
                     packets.emplace_back(std::move(packet));
                  }
                  delayedPackets_.erase(itDelayed);
               }
            }

            while (!pendingPackets_.empty()) {
               packets.push_back(pendingPackets_.front());
               pendingPackets_.pop();
            }
         }

         if (!packets.empty()) {
            for (const auto& packet : packets) {
               if (packet == nullptr) {
                  continue;
               }
               processPacket(*packet);
            }
            packets.clear();
         }
      }
   }

protected:
   std::atomic_bool                       processingHalted_{ false };

private:
   std::thread processingThread_;
   std::atomic_bool                       continueExecution_{ true };
   mutable std::atomic_flag               pendingPacketsLock_ = ATOMIC_FLAG_INIT;
   ManualResetEvent                       pendingPacketsEvent_;
   std::queue<std::shared_ptr<T>>         pendingPackets_;
};

#endif // __PROCESSING_THREAD_H__
