#ifndef __SYSTEM_TIME_H__
#define __SYSTEM_TIME_H__

#include <chrono>

namespace sytem_time
{
   using system_clock_t = std::chrono::system_clock;
   using time_point_t = system_clock_t::time_point;

   uint64_t getTimestampUTC();
}

#endif // __SYSTEM_TIME_H__
