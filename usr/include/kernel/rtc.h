#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include <stdint.h>

uint32_t rtc_read_epoch(void);                       // seconds since 1970-01-01 00:00:00 from the CMOS clock

// wall-clock time: the CMOS clock is read once, then the timer keeps it running
void     time_now(uint32_t *sec, uint32_t *usec);
uint32_t time_uptime_ticks(void);

#endif