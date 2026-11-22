#include <time.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "internal.h"

static const char *const day_abbr[]   = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const day_name[]   = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const month_abbr[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const month_name[] = { "January", "February", "March", "April", "May", "June",
                                          "July", "August", "September", "October", "November", "December" };

static long days_from_civil(long y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, long *y, int *m, int *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yy + (*m <= 2);
}

struct tm *gmtime_r(const time_t *t, struct tm *tm) {
    long secs = (long)*t;
    long days = secs / 86400;
    long rem  = secs % 86400;
    if (rem < 0) { rem += 86400; days--; }

    long y;
    int m, d;
    civil_from_days(days, &y, &m, &d);

    tm->tm_year = (int)(y - 1900);
    tm->tm_mon  = m - 1;
    tm->tm_mday = d;
    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min  = (int)((rem % 3600) / 60);
    tm->tm_sec  = (int)(rem % 60);
    tm->tm_wday = (int)(((days % 7) + 11) % 7);               // 1970-01-01 was a Thursday
    tm->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
    return tm;
}

struct tm *gmtime(const time_t *t) {
    static struct tm tm;
    return gmtime_r(t, &tm);
}

struct tm *localtime_r(const time_t *t, struct tm *tm) { return gmtime_r(t, tm); }   // the clock runs in UTC
struct tm *localtime(const time_t *t)                  { return gmtime(t); }

time_t timegm(struct tm *tm) {
    long y = tm->tm_year + 1900L;
    long mon = tm->tm_mon;

    y += mon / 12;
    mon %= 12;
    if (mon < 0) { mon += 12; y--; }

    long days = days_from_civil(y, (int)mon + 1, 1) + (tm->tm_mday - 1);
    time_t t = (time_t)(days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec);
    gmtime_r(&t, tm);                                          // normalise the fields
    return t;
}

time_t mktime(struct tm *tm) { return timegm(tm); }

time_t time(time_t *t) {
    long r = __sys_ret(__syscall3(SYS_time, 0, 0, 0));
    if (t) *t = (time_t)r;
    return (time_t)r;
}

double difftime(time_t a, time_t b) { return (double)(a - b); }

clock_t clock(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return (clock_t)-1;
    return (clock_t)ts.tv_sec * CLOCKS_PER_SEC + ts.tv_nsec / 1000;
}

char *asctime(const struct tm *tm) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%s %s %2d %02d:%02d:%02d %d\n",
             day_abbr[tm->tm_wday % 7], month_abbr[tm->tm_mon % 12], tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
    return buf;
}

char *ctime(const time_t *t) {
    struct tm tm;
    return asctime(gmtime_r(t, &tm));
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    size_t n = 0;

#define PUT(str) do { const char *_p = (str); while (*_p) { if (n + 1 >= max) return 0; s[n++] = *_p++; } } while (0)
#define NUM(w, v) do { char _b[16]; snprintf(_b, sizeof(_b), "%0" #w "d", (int)(v)); PUT(_b); } while (0)

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (n + 1 >= max) return 0;
            s[n++] = *fmt;
            continue;
        }
        switch (*++fmt) {
        case 'a': PUT(day_abbr[tm->tm_wday % 7]); break;
        case 'A': PUT(day_name[tm->tm_wday % 7]); break;
        case 'b': case 'h': PUT(month_abbr[tm->tm_mon % 12]); break;
        case 'B': PUT(month_name[tm->tm_mon % 12]); break;
        case 'c': { char b[40]; strftime(b, sizeof(b), "%a %b %e %H:%M:%S %Y", tm); PUT(b); break; }
        case 'C': NUM(2, (tm->tm_year + 1900) / 100); break;
        case 'd': NUM(2, tm->tm_mday); break;
        case 'D': { char b[16]; strftime(b, sizeof(b), "%m/%d/%y", tm); PUT(b); break; }
        case 'e': { char b[8]; snprintf(b, sizeof(b), "%2d", tm->tm_mday); PUT(b); break; }
        case 'F': { char b[16]; strftime(b, sizeof(b), "%Y-%m-%d", tm); PUT(b); break; }
        case 'H': NUM(2, tm->tm_hour); break;
        case 'I': NUM(2, tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12); break;
        case 'j': NUM(3, tm->tm_yday + 1); break;
        case 'm': NUM(2, tm->tm_mon + 1); break;
        case 'M': NUM(2, tm->tm_min); break;
        case 'n': PUT("\n"); break;
        case 'p': PUT(tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'r': { char b[16]; strftime(b, sizeof(b), "%I:%M:%S %p", tm); PUT(b); break; }
        case 'R': { char b[8]; strftime(b, sizeof(b), "%H:%M", tm); PUT(b); break; }
        case 's': { struct tm t = *tm; char b[16]; snprintf(b, sizeof(b), "%ld", (long)timegm(&t)); PUT(b); break; }
        case 'S': NUM(2, tm->tm_sec); break;
        case 't': PUT("\t"); break;
        case 'T': case 'X': { char b[16]; strftime(b, sizeof(b), "%H:%M:%S", tm); PUT(b); break; }
        case 'u': NUM(1, tm->tm_wday == 0 ? 7 : tm->tm_wday); break;
        case 'w': NUM(1, tm->tm_wday); break;
        case 'x': { char b[16]; strftime(b, sizeof(b), "%m/%d/%y", tm); PUT(b); break; }
        case 'y': NUM(2, (tm->tm_year + 1900) % 100); break;
        case 'Y': { char b[16]; snprintf(b, sizeof(b), "%d", tm->tm_year + 1900); PUT(b); break; }
        case 'z': PUT("+0000"); break;
        case 'Z': PUT("UTC"); break;
        case '%': PUT("%"); break;
        default:  break;
        }
    }
    if (max == 0) return 0;
    s[n] = '\0';
    return n;
#undef PUT
#undef NUM
}