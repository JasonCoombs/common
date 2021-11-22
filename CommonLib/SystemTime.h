#ifndef __SYSTEM_TIME_H__
#define __SYSTEM_TIME_H__

#include <chrono>

namespace sytem_time
{
   using system_clock_t = std::chrono::system_clock;
   using time_point_t = system_clock_t::time_point;

   uint64_t getTimestampUTC();

   template<class D>
   uint64_t getDurationMS(const D& duration)
   {
      return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
   }
}

#endif // __SYSTEM_TIME_H__
