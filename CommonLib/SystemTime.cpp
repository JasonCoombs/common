#include "SystemTime.h"

uint64_t sytem_time::getTimestampUTC()
{
   return getTimestampUTC(sytem_time::system_clock_t::now());
}

uint64_t sytem_time::getTimestampUTC(const time_point_t& timepoint)
{
   return std::chrono::duration_cast<std::chrono::milliseconds>(timepoint.time_since_epoch()).count();
}
