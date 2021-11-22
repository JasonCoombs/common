#include "SystemTime.h"

uint64_t sytem_time::getTimestampUTC()
{
   const auto currentTime = sytem_time::system_clock_t::now();
   return std::chrono::duration_cast<std::chrono::milliseconds>(currentTime.time_since_epoch()).count();
}
