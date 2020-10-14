/*

***********************************************************************************
* Copyright (C) 2020 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef WS_COMMON_PRIVATE_H
#define WS_COMMON_PRIVATE_H

#include <chrono>
#include <functional>
#include <map>
#include <memory>

struct lws_context;
struct lws_sorted_usec_list;

namespace bs {
   namespace network {
      namespace ws {

         class WsTimerHelperData;

         class WsTimerHelper
         {
         public:
            WsTimerHelper();
            ~WsTimerHelper();

            using TimerCallback = std::function<void()>;

            void scheduleCallback(lws_context *context, std::chrono::milliseconds timeout, TimerCallback callback);

            void clear();

         private:
            static void timerCallback(lws_sorted_usec_list *list);

            std::map<uint64_t, std::unique_ptr<WsTimerHelperData>> timers_;
            uint64_t nextTimerId_{};
         };

      };
   }
}

#endif // WS_COMMON_PRIVATE_H
