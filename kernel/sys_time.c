#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "syscall.h"
#include "proc.h"
#include "paging.h"
#include "pit.h"
#include "rtc.h"

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

// time(t): 13
static int sys_time(struct registers *r) {
    int *t = (int *)(uintptr_t)r->ebx;
    uint32_t sec, usec;

    time_now(&sec, &usec);
    if (t) {
        if (!paging_check_user((uint32_t)(uintptr_t)t, sizeof(int), 1)) return -EFAULT;
        *t = (int)sec;
    }
    return (int)sec;
}

// gettimeofday(tv, tz): 78
static int sys_gettimeofday(struct registers *r) {
    struct timeval *tv = (struct timeval *)(uintptr_t)r->ebx;
    uint32_t sec, usec;

    if (!tv) return 0;
    if (!paging_check_user((uint32_t)(uintptr_t)tv, sizeof(*tv), 1)) return -EFAULT;

    time_now(&sec, &usec);
    tv->tv_sec  = (int)sec;
    tv->tv_usec = (int)usec;
    return 0;
}

// settimeofday(tv, tz): 79. Root only. The time zone is ignored (there are none), and so are
// the microseconds: the clock is set to whole seconds, in the CMOS clock too.
static int sys_settimeofday(struct registers *r) {
    const struct timeval *tv = (const struct timeval *)(uintptr_t)r->ebx;
    struct timeval t;

    if (current->uid != 0) return -EPERM;
    if (!tv) return 0;                              // only a time zone was given: nothing to do
    if (!paging_check_user((uint32_t)(uintptr_t)tv, sizeof(*tv), 0)) return -EFAULT;

    memcpy(&t, tv, sizeof(t));
    if (t.tv_usec < 0 || t.tv_usec >= 1000000) return -EINVAL;

    uint32_t sec = (uint32_t)t.tv_sec;              // unsigned: good until 2106
    time_set(sec);
    rtc_write_epoch(sec);
    return 0;
}

// clock_gettime(clock, ts): 265
static int sys_clock_gettime(struct registers *r) {
    int clock = (int)r->ebx;
    struct timespec *ts = (struct timespec *)(uintptr_t)r->ecx;

    if (!paging_check_user((uint32_t)(uintptr_t)ts, sizeof(*ts), 1)) return -EFAULT;

    if (clock == CLOCK_REALTIME) {
        uint32_t sec, usec;
        time_now(&sec, &usec);
        ts->tv_sec  = (int)sec;
        ts->tv_nsec = (int)(usec * 1000u);
        return 0;
    }
    if (clock == CLOCK_MONOTONIC) {                 // time since boot
        uint32_t hz = pit_freq() ? pit_freq() : 100;
        uint32_t t = time_uptime_ticks();
        ts->tv_sec  = (int)(t / hz);
        ts->tv_nsec = (int)((t % hz) * (1000000000u / hz));
        return 0;
    }
    return -EINVAL;
}

void sys_time_init(void) {
    syscall_register(13,  sys_time);
    syscall_register(78,  sys_gettimeofday);
    syscall_register(79,  sys_settimeofday);
    syscall_register(265, sys_clock_gettime);
}