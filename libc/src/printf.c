#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

#define LEFT  1
#define PLUS  2
#define SPACE 4
#define ALT   8
#define ZERO  16

#define NDIG 120

struct sink {
    FILE  *fp;                      // write to a FILE ...
    char  *buf;                     // ... or into a string
    size_t cap, len;
};

static void emit(struct sink *s, const char *p, size_t n) {
    if (s->fp) {
        fwrite(p, 1, n, s->fp);
    } else if (s->buf) {
        for (size_t i = 0; i < n; i++)
            if (s->len + i + 1 < s->cap) s->buf[s->len + i] = p[i];
    }
    s->len += n;
}

static void pad(struct sink *s, char c, int n) {
    char b[32];

    if (n <= 0) return;
    for (int i = 0; i < 32; i++) b[i] = c;
    while (n > 0) {
        int k = n < 32 ? n : 32;
        emit(s, b, (size_t)k);
        n -= k;
    }
}

// ---------- integers ---------- //

static void fmt_int(struct sink *s, unsigned long long u, unsigned base, int upper,
                    const char *sign, int flags, int width, int prec) {
    char digits[72];
    char *end = digits + sizeof(digits), *p = end;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    const char *prefix = sign;
    int plen = (int)strlen(sign);
    int nonzero = (u != 0);

    if (!(u == 0 && prec == 0)) {
        if ((u >> 32) == 0) {
            unsigned v = (unsigned)u;
            do { *--p = dig[v % base]; v /= base; } while (v);
        } else {
            do { *--p = dig[u % base]; u /= base; } while (u);
        }
    }

    int nd = (int)(end - p);
    int zeros = prec > nd ? prec - nd : 0;

    if (flags & ALT) {
        if (base == 8 && zeros == 0 && (nd == 0 || *p != '0')) zeros = 1;
        else if (base == 16 && nonzero) { prefix = upper ? "0X" : "0x"; plen = 2; }
    }

    int total = plen + zeros + nd;
    int padn = width > total ? width - total : 0;

    if ((flags & ZERO) && !(flags & LEFT) && prec < 0) { zeros += padn; padn = 0; }
    if (!(flags & LEFT)) pad(s, ' ', padn);
    emit(s, prefix, (size_t)plen);
    pad(s, '0', zeros);
    emit(s, p, (size_t)nd);
    if (flags & LEFT) pad(s, ' ', padn);
}

// ---------- floating point (long double arithmetic for accuracy) ---------- //

static int fmt_f(char *buf, long double v, int prec, int alt) {
    char id[420], fd[NDIG + 2];
    int il = 0, n = 0;
    long double ip, fp;

    if (prec > NDIG) prec = NDIG;
    ip = (v < 9.2e18L) ? (long double)(long long)v : v;
    fp = v - ip;

    if (ip < 1.8e19L) {
        unsigned long long u = (unsigned long long)ip;
        char t[24];
        int k = 0;
        do { t[k++] = (char)('0' + u % 10); u /= 10; } while (u);
        while (k) id[il++] = t[--k];
    } else {
        long double div = 1;
        while (ip / div >= 10 && il < 400) div *= 10;
        while (div >= 1 && il < 410) {
            int d = (int)(ip / div);
            if (d > 9) d = 9;
            ip -= d * div;
            id[il++] = (char)('0' + d);
            div /= 10;
        }
    }

    for (int i = 0; i < prec; i++) {
        fp *= 10;
        int d = (int)fp;
        if (d > 9) d = 9;
        fd[i] = (char)('0' + d);
        fp -= d;
    }

    if (fp >= 0.5L) {                               // round half up, carrying leftwards
        int i = prec - 1;
        while (i >= 0) {
            if (fd[i] == '9') { fd[i] = '0'; i--; }
            else { fd[i]++; break; }
        }
        if (i < 0) {
            int j = il - 1;
            while (j >= 0) {
                if (id[j] == '9') { id[j] = '0'; j--; }
                else { id[j]++; break; }
            }
            if (j < 0) { memmove(id + 1, id, (size_t)il); id[0] = '1'; il++; }
        }
    }

    memcpy(buf + n, id, (size_t)il);
    n += il;
    if (prec > 0 || alt) buf[n++] = '.';
    memcpy(buf + n, fd, (size_t)prec);
    n += prec;
    return n;
}

static int fmt_e(char *buf, long double v, int prec, int upper, int alt, int *expo) {
    char dg[NDIG + 2];
    int e10 = 0, n = 0;

    if (prec > NDIG) prec = NDIG;
    if (v != 0) {
        while (v >= 1e16L) { v /= 1e16L; e10 += 16; }
        while (v >= 10)    { v /= 10;    e10++; }
        while (v < 1e-15L) { v *= 1e16L; e10 -= 16; }
        while (v < 1)      { v *= 10;    e10--; }
    }

    int d0 = (int)v;
    if (d0 > 9) d0 = 9;
    v -= d0;
    dg[0] = (char)('0' + d0);
    for (int i = 1; i <= prec; i++) {
        v *= 10;
        int d = (int)v;
        if (d > 9) d = 9;
        dg[i] = (char)('0' + d);
        v -= d;
    }

    if (v >= 0.5L) {
        int i = prec;
        while (i >= 0) {
            if (dg[i] == '9') { dg[i] = '0'; i--; }
            else { dg[i]++; break; }
        }
        if (i < 0) { dg[0] = '1'; e10++; }
    }
    *expo = e10;

    buf[n++] = dg[0];
    if (prec > 0 || alt) buf[n++] = '.';
    for (int i = 1; i <= prec; i++) buf[n++] = dg[i];
    buf[n++] = upper ? 'E' : 'e';
    int e = e10 < 0 ? -e10 : e10;
    buf[n++] = e10 < 0 ? '-' : '+';
    if (e >= 100) buf[n++] = (char)('0' + e / 100);
    buf[n++] = (char)('0' + (e / 10) % 10);
    buf[n++] = (char)('0' + e % 10);
    return n;
}

static int fmt_g(char *buf, long double v, int prec, int upper, int alt) {
    int p = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
    char tmp[NDIG + 40];
    int x, n;

    fmt_e(tmp, v, p - 1, upper, alt, &x);
    if (p > x && x >= -4) n = fmt_f(buf, v, p - 1 - x, alt);
    else                  n = fmt_e(buf, v, p - 1, upper, alt, &x);

    if (!alt) {                                     // drop trailing zeros
        int epos = -1, dot = 0;
        for (int i = 0; i < n; i++) {
            if (buf[i] == 'e' || buf[i] == 'E') { epos = i; break; }
            if (buf[i] == '.') dot = 1;
        }
        int mend = epos < 0 ? n : epos;
        if (dot) {
            while (mend > 0 && buf[mend - 1] == '0') mend--;
            if (mend > 0 && buf[mend - 1] == '.') mend--;
        }
        if (epos >= 0) {
            int tail = n - epos;
            memmove(buf + mend, buf + epos, (size_t)tail);
            n = mend + tail;
        } else {
            n = mend;
        }
    }
    return n;
}

static void fmt_float(struct sink *s, long double v, char conv, int flags, int width, int prec) {
    char body[NDIG + 440];
    int upper = (conv == 'F' || conv == 'E' || conv == 'G');
    int neg = __builtin_signbitl(v) != 0;
    const char *sign = neg ? "-" : (flags & PLUS) ? "+" : (flags & SPACE) ? " " : "";
    int n;
    int finite = 1;

    if (v != v) {
        memcpy(body, upper ? "NAN" : "nan", 3);
        n = 3;
        finite = 0;
        sign = (flags & PLUS) ? "+" : (flags & SPACE) ? " " : "";
    } else if (v - v != 0) {
        memcpy(body, upper ? "INF" : "inf", 3);
        n = 3;
        finite = 0;
    } else {
        if (neg) v = -v;
        if (prec < 0) prec = 6;
        switch (conv) {
        case 'e': case 'E': { int x; n = fmt_e(body, v, prec, upper, flags & ALT, &x); break; }
        case 'g': case 'G': n = fmt_g(body, v, prec, upper, flags & ALT); break;
        default:            n = fmt_f(body, v, prec, flags & ALT); break;
        }
    }

    int plen = (int)strlen(sign);
    int padn = width > plen + n ? width - plen - n : 0;

    if (!(flags & LEFT) && !((flags & ZERO) && finite)) pad(s, ' ', padn);
    emit(s, sign, (size_t)plen);
    if (!(flags & LEFT) && (flags & ZERO) && finite) pad(s, '0', padn);
    emit(s, body, (size_t)n);
    if (flags & LEFT) pad(s, ' ', padn);
}

// ---------- the format interpreter ---------- //

static int do_printf(struct sink *s, const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            const char *e = fmt;
            while (*e && *e != '%') e++;
            emit(s, fmt, (size_t)(e - fmt));
            fmt = e - 1;
            continue;
        }
        fmt++;

        int flags = 0, width = 0, prec = -1, lmod = 0;

        for (;;) {
            if (*fmt == '-') flags |= LEFT;
            else if (*fmt == '+') flags |= PLUS;
            else if (*fmt == ' ') flags |= SPACE;
            else if (*fmt == '#') flags |= ALT;
            else if (*fmt == '0') flags |= ZERO;
            else break;
            fmt++;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { flags |= LEFT; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
            }
            if (prec < 0) prec = -1;
        }
        for (;;) {                                  // 1 long, 2 long long, -1 short, -2 char, 3 long double
            if (*fmt == 'l') lmod = (lmod == 1) ? 2 : 1;
            else if (*fmt == 'h') lmod = (lmod == -1) ? -2 : -1;
            else if (*fmt == 'z' || *fmt == 't') lmod = 1;
            else if (*fmt == 'j') lmod = 2;
            else if (*fmt == 'L') lmod = 3;
            else break;
            fmt++;
        }

        char conv = *fmt;
        if (!conv) break;

        switch (conv) {
        case 'd': case 'i': {
            long long v;
            if (lmod == 2)      v = va_arg(ap, long long);
            else if (lmod == 1) v = va_arg(ap, long);
            else                v = va_arg(ap, int);
            if (lmod == -1) v = (short)v;
            else if (lmod == -2) v = (signed char)v;

            int neg = v < 0;
            unsigned long long u = neg ? 0 - (unsigned long long)v : (unsigned long long)v;
            fmt_int(s, u, 10, 0, neg ? "-" : (flags & PLUS) ? "+" : (flags & SPACE) ? " " : "",
                    flags, width, prec);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v;
            if (lmod == 2)      v = va_arg(ap, unsigned long long);
            else if (lmod == 1) v = va_arg(ap, unsigned long);
            else                v = va_arg(ap, unsigned int);
            if (lmod == -1) v = (unsigned short)v;
            else if (lmod == -2) v = (unsigned char)v;

            unsigned base = conv == 'o' ? 8 : (conv == 'u' ? 10 : 16);
            fmt_int(s, v, base, conv == 'X', "", flags, width, prec);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            int padn = width > 1 ? width - 1 : 0;
            if (!(flags & LEFT)) pad(s, ' ', padn);
            emit(s, &c, 1);
            if (flags & LEFT) pad(s, ' ', padn);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            size_t len = prec < 0 ? strlen(str) : strnlen(str, (size_t)prec);
            int padn = width > (int)len ? width - (int)len : 0;
            if (!(flags & LEFT)) pad(s, ' ', padn);
            emit(s, str, len);
            if (flags & LEFT) pad(s, ' ', padn);
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)va_arg(ap, void *);
            if (v == 0) {
                int padn = width > 5 ? width - 5 : 0;
                if (!(flags & LEFT)) pad(s, ' ', padn);
                emit(s, "(nil)", 5);
                if (flags & LEFT) pad(s, ' ', padn);
            } else {
                fmt_int(s, v, 16, 0, "", flags | ALT, width, prec);
            }
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            long double v = (lmod == 3) ? va_arg(ap, long double) : (long double)va_arg(ap, double);
            fmt_float(s, v, conv, flags, width, prec);
            break;
        }
        case 'n': {
            int *p = va_arg(ap, int *);
            if (p) *p = (int)s->len;
            break;
        }
        case '%':
            emit(s, "%", 1);
            break;
        default:
            emit(s, "%", 1);
            emit(s, &conv, 1);
            break;
        }
    }
    return (int)s->len;
}

// ---------- the public functions ---------- //

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    struct sink s = { f, NULL, 0, 0 };
    int n = do_printf(&s, fmt, ap);
    return ferror(f) ? -1 : n;
}

int vprintf(const char *fmt, va_list ap)  { return vfprintf(stdout, fmt, ap); }

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    struct sink s = { NULL, buf, cap, 0 };
    int n = do_printf(&s, fmt, ap);
    if (cap) buf[s.len < cap ? s.len : cap - 1] = '\0';
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, (size_t)0x7FFFFFFF, fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int vasprintf(char **out, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return -1;

    *out = malloc((size_t)n + 1);
    if (!*out) return -1;
    return vsnprintf(*out, (size_t)n + 1, fmt, ap);
}

int asprintf(char **out, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(out, fmt, ap);
    va_end(ap);
    return n;
}