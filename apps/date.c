#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

typedef unsigned int u32;

struct dt { int year, mon, day, hour, min, sec, wday, yday; };

static const char *const day_names[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const month_names[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

// ---------- calendar maths (UTC, no time zones) ---------- //

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

static void from_unix(u32 t, struct dt *d) {
    u32 days = t / 86400u, rem = t % 86400u;
    int y = 1970, m = 1;

    d->hour = (int)(rem / 3600u);
    d->min  = (int)((rem % 3600u) / 60u);
    d->sec  = (int)(rem % 60u);
    d->wday = (int)((4u + days) % 7u);              // 1 January 1970 was a Thursday

    for (;;) {
        u32 len = is_leap(y) ? 366u : 365u;
        if (days < len) break;
        days -= len;
        y++;
    }
    d->year = y;
    d->yday = (int)days;

    while (days >= (u32)days_in_month(y, m)) {
        days -= (u32)days_in_month(y, m);
        m++;
    }
    d->mon = m;
    d->day = (int)days + 1;
}

// 0 = ok, -1 = not a real date or outside 1970..2106
static int to_unix(int y, int mo, int d, int h, int mi, int s, u32 *out) {
    if (y < 1970 || y > 2106) return -1;
    if (mo < 1 || mo > 12) return -1;
    if (d < 1 || d > days_in_month(y, mo)) return -1;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return -1;

    u32 days = 0;
    for (int yy = 1970; yy < y; yy++) days += is_leap(yy) ? 366u : 365u;
    for (int m = 1; m < mo; m++) days += (u32)days_in_month(y, m);
    days += (u32)(d - 1);

    u32 rem = (u32)h * 3600u + (u32)mi * 60u + (u32)s;
    if (days > 49710u || (days == 49710u && rem > 23295u)) return -1;   // past 7 Feb 2106
    *out = days * 86400u + rem;
    return 0;
}

// ---------- output ---------- //

struct out { char buf[256]; int len; };

static void put_chr(struct out *o, char c) {
    if (o->len < (int)sizeof(o->buf) - 1) o->buf[o->len++] = c;
}

static void put_str(struct out *o, const char *s, int max) {
    for (int i = 0; s[i] && (max < 0 || i < max); i++) put_chr(o, s[i]);
}

static void put_num(struct out *o, u32 v, int width, char padc) {
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = n; i < width; i++) put_chr(o, padc);
    while (n) put_chr(o, t[--n]);
}

static void format(struct out *o, const char *f, const struct dt *d, u32 t) {
    for (; *f; f++) {
        if (*f != '%') { put_chr(o, *f); continue; }
        f++;
        switch (*f) {
        case '\0': put_chr(o, '%'); return;
        case '%': put_chr(o, '%'); break;
        case 'a': put_str(o, day_names[d->wday], 3); break;
        case 'A': put_str(o, day_names[d->wday], -1); break;
        case 'b':
        case 'h': put_str(o, month_names[d->mon - 1], 3); break;
        case 'B': put_str(o, month_names[d->mon - 1], -1); break;
        case 'c': format(o, "%a %b %e %H:%M:%S %Y", d, t); break;
        case 'd': put_num(o, (u32)d->day, 2, '0'); break;
        case 'D': format(o, "%m/%d/%y", d, t); break;
        case 'e': put_num(o, (u32)d->day, 2, ' '); break;
        case 'F': format(o, "%Y-%m-%d", d, t); break;
        case 'H': put_num(o, (u32)d->hour, 2, '0'); break;
        case 'I': put_num(o, (u32)(d->hour % 12 ? d->hour % 12 : 12), 2, '0'); break;
        case 'j': put_num(o, (u32)d->yday + 1, 3, '0'); break;
        case 'm': put_num(o, (u32)d->mon, 2, '0'); break;
        case 'M': put_num(o, (u32)d->min, 2, '0'); break;
        case 'n': put_chr(o, '\n'); break;
        case 'p': put_str(o, d->hour < 12 ? "AM" : "PM", -1); break;
        case 'R': format(o, "%H:%M", d, t); break;
        case 's': put_num(o, t, 1, '0'); break;
        case 'S': put_num(o, (u32)d->sec, 2, '0'); break;
        case 't': put_chr(o, '\t'); break;
        case 'T': format(o, "%H:%M:%S", d, t); break;
        case 'u': put_num(o, (u32)(d->wday ? d->wday : 7), 1, '0'); break;
        case 'w': put_num(o, (u32)d->wday, 1, '0'); break;
        case 'x': format(o, "%d-%m-%Y", d, t); break;
        case 'X': format(o, "%H:%M:%S", d, t); break;
        case 'y': put_num(o, (u32)(d->year % 100), 2, '0'); break;
        case 'Y': put_num(o, (u32)d->year, 1, '0'); break;
        case 'Z': put_str(o, "UTC", -1); break;
        default:  put_chr(o, '%'); put_chr(o, *f); break;
        }
    }
}

// ---------- reading what the user typed ---------- //

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int all_digits(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) if (!is_digit(*s)) return 0;
    return 1;
}

// Digits at *p: 0 = ok (value, and how many digits), -1 = none or too many
static int read_num(const char **p, u32 *v, int *digits) {
    u32 x = 0;
    int n = 0;

    while (is_digit(**p)) {
        if (n >= 9) return -1;
        x = x * 10u + (u32)(**p - '0');
        (*p)++;
        n++;
    }
    if (n == 0) return -1;
    *v = x;
    if (digits) *digits = n;
    return 0;
}

static int parse_unix(const char *s, u32 *out) {
    u32 v = 0;

    if (*s == '@') s++;
    if (!is_digit(*s)) return -1;
    for (; *s; s++) {
        if (!is_digit(*s)) return -1;
        u32 dgt = (u32)(*s - '0');
        if (v > (0xFFFFFFFFu - dgt) / 10u) return -1;       // does not fit in 32 bits
        v = v * 10u + dgt;
    }
    *out = v;
    return 0;
}

// DD-MM-YYYY, DD-MM-YY or YYYY-MM-DD, with - / or . between the parts
static int parse_date(const char *s, int *y, int *m, int *d) {
    u32 a, b, c;
    int da, dc;
    char sep;

    if (read_num(&s, &a, &da)) return -1;
    sep = *s;
    if (sep != '-' && sep != '/' && sep != '.') return -1;
    s++;
    if (read_num(&s, &b, NULL)) return -1;
    if (*s != sep) return -1;
    s++;
    if (read_num(&s, &c, &dc)) return -1;
    if (*s) return -1;

    if (da == 4) {                                          // 2026-11-24
        *y = (int)a; *m = (int)b; *d = (int)c;
    } else {                                                // 24-11-2026 or 24-11-26
        *d = (int)a; *m = (int)b;
        if (dc == 4)      *y = (int)c;
        else if (dc == 2) *y = (int)(c <= 68 ? 2000 + c : 1900 + c);
        else return -1;
    }
    return 0;
}

// HH:MM or HH:MM:SS
static int parse_time(const char *s, int *h, int *m, int *sec) {
    u32 a, b, c = 0;

    if (read_num(&s, &a, NULL)) return -1;
    if (*s++ != ':') return -1;
    if (read_num(&s, &b, NULL)) return -1;
    if (*s == ':') {
        s++;
        if (read_num(&s, &c, NULL)) return -1;
    }
    if (*s) return -1;
    *h = (int)a; *m = (int)b; *sec = (int)c;
    return 0;
}

// A unix time, a date, a time, or a date and a time. Whatever is missing comes from `now`.
static int parse_value(const char *arg, u32 now, u32 *out) {
    char tok[2][32];
    int ntok = 0;
    const char *p = arg;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (ntok == 2) return -1;
        int n = 0;
        while (*p && *p != ' ' && *p != '\t') {
            if (n >= 31) return -1;
            tok[ntok][n++] = *p++;
        }
        tok[ntok][n] = '\0';
        ntok++;
    }
    if (ntok == 0) return -1;

    if (ntok == 1 && (tok[0][0] == '@' || all_digits(tok[0])))
        return parse_unix(tok[0], out);

    struct dt cur;
    from_unix(now, &cur);
    int y = cur.year, mo = cur.mon, d = cur.day;
    int h = cur.hour, mi = cur.min, s = cur.sec;
    int have_date = 0, have_time = 0;

    for (int i = 0; i < ntok; i++) {
        if (strchr(tok[i], ':')) {
            if (have_time || parse_time(tok[i], &h, &mi, &s)) return -1;
            have_time = 1;
        } else {
            if (have_date || parse_date(tok[i], &y, &mo, &d)) return -1;
            have_date = 1;
        }
    }
    return to_unix(y, mo, d, h, mi, s, out);
}

// ---------- main ---------- //

static void usage(void) {
    printf("usage: date [-u] [+FORMAT]\n"
           "       date -d VALUE [+FORMAT]    show another time, without setting it\n"
           "       date -s VALUE              set the date and time\n"
           "VALUE is a unix time (1700000000 or @1700000000), a date (DD-MM-YYYY,\n"
           "DD-MM-YY or YYYY-MM-DD), a time (HH:MM or HH:MM:SS), or a date and a time\n"
           "(\"24-11-2026 14:30\"). What is left out stays as it is now.\n"
           "FORMAT: %%Y %%y %%m %%d %%e %%H %%M %%S %%a %%A %%b %%B %%j %%s %%F %%T %%D %%Z ...\n");
}

static void show(u32 t, const char *fmt) {
    struct dt d;
    struct out o;

    from_unix(t, &d);
    o.len = 0;
    format(&o, fmt ? fmt : "%a %b %e %H:%M:%S %Y", &d, t);
    o.buf[o.len] = '\0';
    printf("%s\n", o.buf);
}

int main(int argc, char **argv) {
    const char *set = NULL, *conv = NULL, *fmt = NULL;
    struct timeval tv;
    u32 now, t;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--utc") == 0) {
            continue;                                       // everything is UTC here anyway
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            set = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            conv = argv[++i];
        } else if (argv[i][0] == '+') {
            fmt = argv[i] + 1;
        } else {
            usage();
            return strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }
    if (set && conv) { usage(); return 1; }

    if (gettimeofday(&tv, NULL) < 0) {
        printf("date: cannot read the clock: %s\n", strerror(errno));
        return 1;
    }
    now = (u32)tv.tv_sec;                                   // unsigned: good until 2106

    if (set) {
        if (parse_value(set, now, &t) != 0) {
            printf("date: invalid date '%s'\n", set);
            return 1;
        }
        tv.tv_sec  = (time_t)t;
        tv.tv_usec = 0;
        if (settimeofday(&tv, NULL) < 0) {
            printf("date: cannot set the date: %s\n", strerror(errno));
            return 1;
        }
        show(t, fmt);
        return 0;
    }

    if (conv) {
        if (parse_value(conv, now, &t) != 0) {
            printf("date: invalid date '%s'\n", conv);
            return 1;
        }
        show(t, fmt);
        return 0;
    }

    show(now, fmt);
    return 0;
}