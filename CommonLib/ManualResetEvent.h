/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __MANUAL_RESET_EVENT_H__
#define __MANUAL_RESET_EVENT_H__

#include <atomic>
#include <condition_variable>
#include <mutex>

class ManualResetEvent
{
public:
   ManualResetEvent();
   ~ManualResetEvent() noexcept = default;

   ManualResetEvent(const ManualResetEvent&) = delete;
   ManualResetEvent& operator = (const ManualResetEvent&) = delete;

   ManualResetEvent(ManualResetEvent&&) = delete;
   ManualResetEvent& operator = (ManualResetEvent&&) = delete;

   bool WaitForEvent(const std::chrono::milliseconds& timeout);
   bool WaitForEvent();

   void SetEvent();
   void ResetEvent();

private:
   std::condition_variable event_;
   mutable std::mutex      flagMutex_;
   bool                    eventFlag_;
};

#endif // __MANUAL_RESET_EVENT_H__
