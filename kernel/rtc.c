#include <stdint.h>
#include "rtc.h"
#include "io.h"
#include "pit.h"

static uint8_t cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void cmos_write(uint8_t reg, uint8_t v) {
    outb(0x70, reg);
    outb(0x71, v);
}

static uint8_t bcd(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

static uint8_t to_bcd(uint8_t v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
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

// ... and the other way round: a day number since 1970-01-01 -> year, month, day
static void civil_from_days(uint32_t days, int *y, int *m, int *d) {
    int z = (int)days + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;

    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)yoe + era * 400 + (*m <= 2);
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

// A value in the format the chip is set to (BCD or binary)
static uint8_t enc(int binary, int v) {
    return binary ? (uint8_t)v : to_bcd((uint8_t)v);
}

int rtc_write_epoch(uint32_t t) {
    uint32_t days = t / 86400u, rem = t % 86400u;
    int year, mon, day;

    civil_from_days(days, &year, &mon, &day);
    int hour = (int)(rem / 3600u);
    int min  = (int)((rem % 3600u) / 60u);
    int sec  = (int)(rem % 60u);

    uint8_t status_b = cmos(0x0B);
    int binary = (status_b & 0x04) != 0;
    int h24    = (status_b & 0x02) != 0;

    // The century register may not exist: trust it only if it holds 19..21, like the reader does
    uint8_t cen_raw = cmos(0x32);
    uint8_t cen = binary ? cen_raw : bcd(cen_raw);
    int have_century = cen >= 19 && cen <= 21;

    uint8_t hv;
    if (h24) {
        hv = enc(binary, hour);
    } else {                                        // 12-hour clock: 1..12 and the PM flag in bit 7
        int h12 = hour % 12 ? hour % 12 : 12;
        hv = enc(binary, h12);
        if (hour >= 12) hv |= 0x80;
    }

    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;      // let a running update finish

    cmos_write(0x0B, (uint8_t)(status_b | 0x80));   // SET: the clock stops updating
    cmos_write(0x00, enc(binary, sec));
    cmos_write(0x02, enc(binary, min));
    cmos_write(0x04, hv);
    cmos_write(0x07, enc(binary, day));
    cmos_write(0x08, enc(binary, mon));
    cmos_write(0x09, enc(binary, year % 100));
    if (have_century) cmos_write(0x32, enc(binary, year / 100));
    cmos_write(0x0B, (uint8_t)(status_b & ~0x80));  // the clock runs again from the new time

    return (!have_century && year < 2000) ? -1 : 0;
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

void time_set(uint32_t sec) {
    uint32_t hz = pit_freq() ? pit_freq() : 100;

    boot_epoch = sec - pit_ticks() / hz;            // time_now() now returns `sec` (plus the tick phase)
    have_boot_time = 1;                             // and must not read the CMOS again
}