#include <stdint.h>
#include "rtc.h"
#include "io.h"
#include "pit.h"

static uint8_t cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
static uint32_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (int)doe - 719468);
}

struct rtc_regs {
    uint8_t sec, min, hour, day, mon, year, century;
};

static void read_regs(struct rtc_regs *r) {
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;      // wait out an update in progress

    r->sec     = cmos(0x00);
    r->min     = cmos(0x02);
    r->hour    = cmos(0x04);
    r->day     = cmos(0x07);
    r->mon     = cmos(0x08);
    r->year    = cmos(0x09);
    r->century = cmos(0x32);
}

uint32_t rtc_read_epoch(void) {
    struct rtc_regs a, b;

    read_regs(&a);                                  // read until two reads agree
    for (int tries = 0; tries < 5; tries++) {
        read_regs(&b);
        if (a.sec == b.sec && a.min == b.min && a.hour == b.hour && a.day == b.day &&
            a.mon == b.mon && a.year == b.year)
            break;
        a = b;
    }

    uint8_t status_b = cmos(0x0B);
    int pm = (a.hour & 0x80) != 0;

    if (!(status_b & 0x04)) {                       // values are BCD
        a.sec = bcd(a.sec);
        a.min = bcd(a.min);
        a.hour = (uint8_t)(bcd(a.hour & 0x7F) | (a.hour & 0x80));
        a.day = bcd(a.day);
        a.mon = bcd(a.mon);
        a.year = bcd(a.year);
        a.century = bcd(a.century);
    }
    if (!(status_b & 0x02)) {                       // 12-hour clock
        a.hour &= 0x7F;
        if (pm) a.hour = (uint8_t)((a.hour % 12) + 12);
        else if (a.hour == 12) a.hour = 0;
    }

    int year = (a.century >= 19 && a.century <= 21) ? a.century * 100 + a.year : 2000 + a.year;
    if (a.mon < 1 || a.mon > 12 || a.day < 1 || a.day > 31) return 0;

    return days_from_civil(year, a.mon, a.day) * 86400u +
           (uint32_t)a.hour * 3600u + (uint32_t)a.min * 60u + a.sec;
}

static int      have_boot_time;
static uint32_t boot_epoch;                         // wall-clock seconds at tick 0

static void init_time(void) {
    uint32_t hz = pit_freq() ? pit_freq() : 100;
    boot_epoch = rtc_read_epoch() - pit_ticks() / hz;
    have_boot_time = 1;
}

uint32_t time_uptime_ticks(void) {
    return pit_ticks();
}

void time_now(uint32_t *sec, uint32_t *usec) {
    uint32_t hz = pit_freq() ? pit_freq() : 100;
    uint32_t t = pit_ticks();

    if (!have_boot_time) init_time();
    *sec  = boot_epoch + t / hz;
    *usec = (t % hz) * (1000000u / hz);
}