#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include <stdint.h>

uint32_t rtc_read_epoch(void);                       // seconds since 1970-01-01 00:00:00 from the CMOS clock

// wall-clock time: the CMOS clock is read once, then the timer keeps it running
void     time_now(uint32_t *sec, uint32_t *usec);
uint32_t time_uptime_ticks(void);

// Sets the wall clock to `sec` seconds since 1970 (whole seconds; the CMOS is not touched)
void     time_set(uint32_t sec);

// Writes `sec` into the CMOS clock so it survives a reboot.
// 0 = ok, -1 = written, but a year before 2000 cannot be stored exactly on this chip
int      rtc_write_epoch(uint32_t sec);

#endif