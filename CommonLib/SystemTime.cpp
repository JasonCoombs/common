#include "SystemTime.h"

uint64_t system_time::getTimestampUTC()
{
   return getTimestampUTC(system_time::system_clock_t::now());
}

uint64_t system_time::getTimestampUTC(const time_point_t& timepoint)
{
   return std::chrono::duration_cast<std::chrono::milliseconds>(timepoint.time_since_epoch()).count();
}
