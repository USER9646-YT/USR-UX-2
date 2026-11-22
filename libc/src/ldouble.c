#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 2^n built directly from the 80-bit format (|n| within the extended range)
static long double pow2i(int n) {
    union {
        long double ld;
        struct { unsigned long long m; unsigned short se; } p;
    } u;

    if (n > 16383)  return __builtin_huge_vall();
    if (n < -16382) return 0.0L;
    u.ld = 0.0L;
    u.p.m  = 0x8000000000000000ULL;
    u.p.se = (unsigned short)(n + 16383);
    return u.ld;
}

long double ldexpl(long double x, int n) {
    if (x == 0.0L || x != x || __builtin_isinfl(x)) return x;
    if (n > 40000)  n = 40000;
    if (n < -40000) n = -40000;

    while (n > 16000)  { x *= pow2i(16000);  n -= 16000; }     // stay inside the exponent range
    while (n < -16000) { x *= pow2i(-16000); n += 16000; }
    return x * pow2i(n);
}

float ldexpf(float x, int n) {
    return (float)ldexpl((long double)x, n);
}

long double strtold(const char *s, char **end) {
    const char *p = s;
    int neg = 0, digits = 0, exp10 = 0;
    long double val = 0;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }

    if (strncasecmp(p, "inf", 3) == 0) {
        p += strncasecmp(p, "infinity", 8) == 0 ? 8 : 3;
        if (end) *end = (char *)p;
        return neg ? -__builtin_huge_vall() : __builtin_huge_vall();
    }
    if (strncasecmp(p, "nan", 3) == 0) {
        if (end) *end = (char *)(p + 3);
        return __builtin_nanl("");
    }

    while (isdigit((unsigned char)*p)) { val = val * 10 + (*p - '0'); p++; digits++; }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) { val = val * 10 + (*p - '0'); exp10--; p++; digits++; }
    }
    if (!digits) {
        if (end) *end = (char *)s;
        return 0;
    }

    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int eneg = 0, e = 0;
        if (*q == '+') q++;
        else if (*q == '-') { eneg = 1; q++; }
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) { if (e < 10000) e = e * 10 + (*q - '0'); q++; }
            exp10 += eneg ? -e : e;
            p = q;
        }
    }

    if (exp10 != 0) {
        long double scale = 1, b = 10;
        int n = exp10 < 0 ? -exp10 : exp10;
        while (n) { if (n & 1) scale *= b; b *= b; n >>= 1; }
        val = exp10 < 0 ? val / scale : val * scale;
    }

    if (end) *end = (char *)p;
    return neg ? -val : val;
}