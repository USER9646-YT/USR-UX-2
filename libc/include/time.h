#ifndef _TIME_H
#define _TIME_H
#include <stddef.h>
#include <sys/types.h>

#define CLOCKS_PER_SEC 1000000L
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec { time_t tv_sec; long tv_nsec; };

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};

time_t time(time_t *);
double difftime(time_t, time_t);
clock_t clock(void);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
time_t mktime(struct tm *);
time_t timegm(struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);
char *asctime(const struct tm *);
char *ctime(const time_t *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(int, struct timespec *);
#endif