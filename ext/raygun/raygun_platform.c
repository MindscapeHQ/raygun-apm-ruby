#include "raygun.h"
#include "raygun_platform.h"

// X platform getpid - works for Mac OS, ming32 and Linux
rg_unsigned_int_t rg_getpid()
{
  return (rg_unsigned_int_t)getpid();
}

// The high resolution timestamper applied to all events - works for Mac OS, mingw32 and Linux
rg_timestamp_t rg_timestamp()
{
  struct timeval time;
  gettimeofday(&time, NULL);
  return ((rg_timestamp_t)time.tv_sec * TIMESTAMP_UNITS_PER_SECOND + time.tv_usec);
}
