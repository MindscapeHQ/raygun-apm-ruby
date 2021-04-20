#ifndef RAYGUN_PLATFORM_H
#define RAYGUN_PLATFORM_H

// X platform concerns - currently just timestamp and pids, forward looking memory mapping,
// file management etc.

#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>

// ruby 2.5
#ifndef USHORT2NUM
#define USHORT2NUM(x) RB_INT2FIX(x)
#endif

// CT_PROCESS_FREQUENCY
#define TIMESTAMP_UNITS_PER_SECOND 1000000 // usec

rg_unsigned_int_t rg_getpid();
rg_timestamp_t rg_timestamp();

#endif
