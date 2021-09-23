/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ManualResetEvent.h"

#include <chrono>

ManualResetEvent::ManualResetEvent()
: eventFlag_(false)
{}

bool ManualResetEvent::WaitForEvent(const std::chrono::milliseconds& period)
{
   std::unique_lock<std::mutex> locker(flagMutex_);
   return (event_.wait_for(locker, period) == std::cv_status::no_timeout) && eventFlag_;
}  // waiting with predicate consumes 100% CPU, at least on Linux
// https://stackoverflow.com/questions/67762275/wait-of-condition-variable-in-c-will-consume-cpu-resource

bool ManualResetEvent::WaitForEvent()
{  // same applies here (on Linux) - need to build with -pthread which is not the case apparently
   std::unique_lock<std::mutex>  locker(flagMutex_);
   event_.wait(locker);
//      [this] () { return eventFlag_.load(); }
//      );
   return eventFlag_;
}

void ManualResetEvent::SetEvent()
{
   std::unique_lock<std::mutex>  locker(flagMutex_);
   if (!eventFlag_) {
      eventFlag_ = true;
      event_.notify_all();
   }
}

void ManualResetEvent::ResetEvent()
{
   std::unique_lock<std::mutex>  locker(flagMutex_);
   eventFlag_ = false;
   event_.notify_all();
}
