#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <sys/types.h>

struct timeval  { time_t tv_sec; suseconds_t tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };

int gettimeofday(struct timeval *, struct timezone *);

// As in glibc, <sys/time.h> also provides select() and fd_set. Included last, because
// <sys/select.h> needs struct timeval.
#include <sys/select.h>
#endif