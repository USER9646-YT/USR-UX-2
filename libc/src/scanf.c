#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "internal.h"

struct in {
    FILE       *fp;
    const char *s;
    int         pos;
    int         count;                  // characters consumed, for %n
};

static int gc(struct in *in) {
    int c;
    if (in->fp) {
        c = fgetc(in->fp);
    } else {
        c = (unsigned char)in->s[in->pos];
        if (c == 0) return EOF;
        in->pos++;
    }
    if (c != EOF) in->count++;
    return c;
}

static void ugc(struct in *in, int c) {
    if (c == EOF) return;
    if (in->fp) ungetc(c, in->fp);
    else in->pos--;
    in->count--;
}

static int skip_ws(struct in *in) {
    int c;
    do { c = gc(in); } while (c != EOF && isspace(c));
    ugc(in, c);
    return c != EOF;
}

// reads an integer into *out; returns 1 on success
static int scan_int(struct in *in, int base, int width, int *neg, unsigned long long *out) {
    int c, any = 0;
    unsigned long long v = 0;

    if (!skip_ws(in)) return 0;
    *neg = 0;

    c = gc(in);
    width--;
    if (c == '+' || c == '-') {
        *neg = (c == '-');
        c = (width > 0) ? gc(in) : EOF;
        width--;
    }
    if ((base == 0 || base == 16) && c == '0' && width > 0) {
        int c2 = gc(in);
        width--;
        if (c2 == 'x' || c2 == 'X') {
            base = 16;
            c = (width > 0) ? gc(in) : EOF;
            width--;
        } else {
            ugc(in, c2);
            width++;
            if (base == 0) base = 8;
            any = 1;
        }
    }
    if (base == 0) base = 10;

    for (;;) {
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;

        any = 1;
        v = v * (unsigned)base + (unsigned)d;
        if (width <= 0) { c = EOF; break; }
        c = gc(in);
        width--;
    }
    if (c != EOF) ugc(in, c);
    *out = v;
    return any;
}

static int scan_float(struct in *in, int width, double *out) {
    char buf[128];
    int n = 0, c;

    if (!skip_ws(in)) return 0;
    if (width <= 0 || width > 127) width = 127;

    c = gc(in);
    if ((c == '+' || c == '-') && n < width) { buf[n++] = (char)c; c = gc(in); }
    while (n < width && (isdigit(c) || c == '.' || c == 'e' || c == 'E' ||
                         (n > 0 && (c == '+' || c == '-') && (buf[n - 1] == 'e' || buf[n - 1] == 'E')) ||
                         isalpha(c))) {
        buf[n++] = (char)c;
        c = gc(in);
    }
    ugc(in, c);
    buf[n] = '\0';

    char *end;
    *out = strtod(buf, &end);
    return end != buf;
}

static int in_set(const char *set, const char *setend, int c) {
    int negate = 0;
    const char *p = set;

    if (*p == '^') { negate = 1; p++; }
    int hit = 0;
    for (; p < setend; p++) {
        if (p + 2 < setend && p[1] == '-') {
            if (c >= (unsigned char)p[0] && c <= (unsigned char)p[2]) hit = 1;
            p += 2;
        } else if ((unsigned char)*p == c) {
            hit = 1;
        }
    }
    return hit != negate;
}

static int do_scanf(struct in *in, const char *fmt, va_list ap) {
    int assigned = 0, failed_input = 0;

    for (; *fmt; fmt++) {
        if (isspace((unsigned char)*fmt)) {
            skip_ws(in);
            continue;
        }
        if (*fmt != '%') {
            int c = gc(in);
            if (c != (unsigned char)*fmt) { ugc(in, c); if (c == EOF) failed_input = 1; goto done; }
            continue;
        }
        fmt++;

        int suppress = 0, width = 0, lmod = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        for (;;) {
            if (*fmt == 'l') lmod = (lmod == 1) ? 2 : 1;
            else if (*fmt == 'h') lmod = (lmod == -1) ? -2 : -1;
            else if (*fmt == 'L' || *fmt == 'j' || *fmt == 'z') lmod = 2;
            else break;
            fmt++;
        }

        char conv = *fmt;
        if (!conv) break;

        switch (conv) {
        case '%': {
            skip_ws(in);
            int c = gc(in);
            if (c != '%') { ugc(in, c); goto done; }
            break;
        }
        case 'n':
            if (!suppress) *va_arg(ap, int *) = in->count;
            break;
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X') ? 16 : (conv == 'i' ? 0 : 10);
            int neg;
            unsigned long long v;
            if (!scan_int(in, base, width ? width : 1 << 20, &neg, &v)) {
                if (!skip_ws(in)) failed_input = 1;
                goto done;
            }
            if (neg) v = 0 - v;
            if (!suppress) {
                if (lmod == 2)       *va_arg(ap, long long *) = (long long)v;
                else if (lmod == 1)  *va_arg(ap, long *) = (long)v;
                else if (lmod == -1) *va_arg(ap, short *) = (short)v;
                else if (lmod == -2) *va_arg(ap, signed char *) = (signed char)v;
                else                 *va_arg(ap, int *) = (int)v;
                assigned++;
            }
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': {
            double d;
            if (!scan_float(in, width, &d)) {
                if (!skip_ws(in)) failed_input = 1;
                goto done;
            }
            if (!suppress) {
                if (lmod >= 1) *va_arg(ap, double *) = d;
                else           *va_arg(ap, float *) = (float)d;
                assigned++;
            }
            break;
        }
        case 's': {
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int n = 0, c;
            if (!skip_ws(in)) { failed_input = 1; goto done; }
            if (width <= 0) width = 1 << 20;
            while (n < width && (c = gc(in)) != EOF) {
                if (isspace(c)) { ugc(in, c); break; }
                if (dst) dst[n] = (char)c;
                n++;
            }
            if (dst) dst[n] = '\0';
            if (!suppress) assigned++;
            break;
        }
        case 'c': {
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int n = width ? width : 1, i = 0;
            for (; i < n; i++) {
                int c = gc(in);
                if (c == EOF) break;
                if (dst) dst[i] = (char)c;
            }
            if (i < n) { failed_input = (i == 0); goto done; }
            if (!suppress) assigned++;
            break;
        }
        case '[': {
            const char *set = ++fmt;
            if (*fmt == '^') fmt++;
            if (*fmt == ']') fmt++;
            while (*fmt && *fmt != ']') fmt++;
            const char *setend = fmt;

            char *dst = suppress ? NULL : va_arg(ap, char *);
            int n = 0, c;
            if (width <= 0) width = 1 << 20;
            while (n < width && (c = gc(in)) != EOF) {
                if (!in_set(set, setend, c)) { ugc(in, c); break; }
                if (dst) dst[n] = (char)c;
                n++;
            }
            if (n == 0) { failed_input = 1; goto done; }
            if (dst) dst[n] = '\0';
            if (!suppress) assigned++;
            break;
        }
        default:
            goto done;
        }
    }

done:
    if (assigned == 0 && failed_input) return EOF;
    return assigned;
}

int vsscanf(const char *s, const char *fmt, va_list ap) {
    struct in in = { NULL, s, 0, 0 };
    return do_scanf(&in, fmt, ap);
}

int vfscanf(FILE *f, const char *fmt, va_list ap) {
    struct in in = { f, NULL, 0, 0 };
    return do_scanf(&in, fmt, ap);
}

int vscanf(const char *fmt, va_list ap) {
    return vfscanf(stdin, fmt, ap);
}

int sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}

int fscanf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfscanf(f, fmt, ap);
    va_end(ap);
    return n;
}

int scanf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return n;
}