/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "WsCommonPrivate.h"

#include <cassert>
#include <cstring>

#include <libwebsockets.h>

using namespace bs::network::ws;

struct WsTimerHelperDataInt : lws_sorted_usec_list_t
{
   WsTimerHelperData *owner;
};

class bs::network::ws::WsTimerHelperData
{
public:
   WsTimerHelper *owner_{};
   WsTimerHelperDataInt timerInt_;
   uint64_t timerId_{};
   WsTimerHelper::TimerCallback callback_;
};

WsTimerHelper::WsTimerHelper() = default;
WsTimerHelper::~WsTimerHelper() = default;

void WsTimerHelper::scheduleCallback(lws_context *context, std::chrono::milliseconds timeout, WsTimerHelper::TimerCallback callback)
{
   auto timerId = nextTimerId_;
   nextTimerId_ += 1;

   auto timer = std::make_unique<bs::network::ws::WsTimerHelperData>();
   std::memset(&timer->timerInt_, 0, sizeof(timer->timerInt_));
   timer->timerInt_.owner = timer.get();
   timer->owner_ = this;
   timer->timerId_ = timerId;
   timer->callback_ = std::move(callback);

   lws_sul_schedule(context, 0, &timer->timerInt_, timerCallback, static_cast<lws_usec_t>(timeout / std::chrono::microseconds(1)));

   timers_.insert(std::make_pair(timerId, std::move(timer)));
}

void WsTimerHelper::clear()
{
   timers_.clear();
   nextTimerId_ = {};
}

void WsTimerHelper::timerCallback(lws_sorted_usec_list *list)
{
   auto dataInt = static_cast<WsTimerHelperDataInt*>(list);
   auto data = dataInt->owner;
   data->callback_();
   auto count = data->owner_->timers_.erase(data->timerId_);
   assert(count == 1);
}
