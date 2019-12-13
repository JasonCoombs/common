/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include <chrono>
#include <thread>
#include "FastLock.h"


FastLock::FastLock(std::atomic_flag &flag_to_lock, bool acquire)
    : flag_(flag_to_lock)
{
   if (acquire) {
      while (std::atomic_flag_test_and_set_explicit(&flag_, std::memory_order_acquire)) {
         std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
      owns_ = true;
   }
   else {
      if (!std::atomic_flag_test_and_set_explicit(&flag_, std::memory_order_acquire)) {
         owns_ = true;
      }
   }
}

FastLock::~FastLock()
{
   std::atomic_flag_clear_explicit(&flag_, std::memory_order_release);
}
