#include <math.h>
#include <errno.h>

#define LN2     0.693147180559945309417232121458L
#define LOG10_2 0.301029995663981195213738894724L
#define LOG2E   1.442695040888963407359924681002L
#define PIL     3.141592653589793238462643383280L

#define INF   (__builtin_huge_val())
#define NANV  (__builtin_nan(""))
#define TWO53 9007199254740992.0

// baited nobaldi award

// ---------- the x87 instructions ---------- //

static long double x_rint(long double x) {
    long double r;
    __asm__ ("frndint" : "=t"(r) : "0"(x));
    return r;
}

static long double x_f2xm1(long double x) {                 // 2^x - 1, |x| <= 1
    long double r;
    __asm__ ("f2xm1" : "=t"(r) : "0"(x));
    return r;
}

static long double x_sqrt(long double x) {
    long double r;
    __asm__ ("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

static long double x_sin(long double x) {
    long double r;
    __asm__ ("fsin" : "=t"(r) : "0"(x));
    return r;
}

static long double x_cos(long double x) {
    long double r;
    __asm__ ("fcos" : "=t"(r) : "0"(x));
    return r;
}

static long double x_tan(long double x) {
    long double r;
    __asm__ ("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}

static long double x_fyl2x(long double x, long double y) {         // y * log2(x)
    long double r;
    __asm__ ("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x_fyl2xp1(long double x, long double y) {       // y * log2(x + 1), |x| < 0.29
    long double r;
    __asm__ ("fyl2xp1" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x_fpatan(long double y, long double x) {        // atan2(y, x)
    long double r;
    __asm__ ("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x_fprem(long double x, long double y) {         // C fmod semantics
    long double r, junk;
    int status;

    __asm__ __volatile__ ("1: fprem\n\t"
                          "fnstsw %%ax\n\t"
                          "sahf\n\t"
                          "jp 1b"
                          : "=t"(r), "=u"(junk), "=a"(status)
                          : "0"(x), "1"(y)
                          : "cc");
    return r;
}

// ---------- helpers in extended precision ---------- //

// 2^n for an integer n, built directly from the 80-bit format
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

static long double exp2l_(long double x) {
    if (x != x) return x;
    if (x >= 16384.0L)  return __builtin_huge_vall();
    if (x <= -16446.0L) return 0.0L;

    long double n = x_rint(x);
    long double p = x_f2xm1(x - n) + 1.0L;          // 2^frac, within [0.707, 1.414]
    int ni = (int)n;
    int h = ni / 2;                                  // two steps: each scale stays representable
    return p * pow2i(h) * pow2i(ni - h);
}

static long double expm1l_(long double x) {
    long double y = x * LOG2E;

    if (y >= -1.0L && y <= 1.0L) return x_f2xm1(y);
    return exp2l_(y) - 1.0L;
}

static long double logl_(long double x) {
    return x_fyl2x(x, LN2);
}

static long double log1pl_(long double x) {
    if (x > -0.29L && x < 0.29L) return x_fyl2xp1(x, LN2);
    return x_fyl2x(1.0L + x, LN2);
}

static int is_integer(double y) {
    return __builtin_fabs(y) >= TWO53 || y == (double)(long long)y;
}

static int is_odd(double y) {
    return __builtin_fabs(y) < TWO53 && y == (double)(long long)y && (((long long)y) & 1);
}

// ---------- classification and rounding ---------- //

double fabs(double x)            { return __builtin_fabs(x); }
float  fabsf(float x)            { return __builtin_fabsf(x); }
long double fabsl(long double x) { return __builtin_fabsl(x); }
double copysign(double x, double y) { return __builtin_copysign(x, y); }

double fmin(double a, double b) { return (a < b || b != b) ? a : b; }
double fmax(double a, double b) { return (a > b || b != b) ? a : b; }

double trunc(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;          // already integral, or inf / nan
    return copysign((double)(long long)x, x);
}

double floor(double x) {
    double t = trunc(x);
    return t > x ? t - 1.0 : t;
}

double ceil(double x) {
    double t = trunc(x);
    return t < x ? t + 1.0 : t;
}

double round(double x) {                                    // halves go away from zero
    double t = trunc(x);
    if (fabs(x - t) >= 0.5) t += copysign(1.0, x);
    return t;
}

double rint(double x)      { return (double)x_rint(x); }
double nearbyint(double x) { return (double)x_rint(x); }
long lround(double x)      { return (long)round(x); }
long lrint(double x)       { return (long)x_rint(x); }

double fmod(double x, double y) {
    if (y == 0.0 || __builtin_isinf(x)) { errno = EDOM; return NANV; }
    return (double)x_fprem(x, y);
}

double ldexp(double x, int n) {
    if (x == 0.0 || x != x || __builtin_isinf(x)) return x;
    if (n > 3000)  n = 3000;
    if (n < -3000) n = -3000;
    int h = n / 2;
    return (double)((long double)x * pow2i(h) * pow2i(n - h));
}

double frexp(double x, int *e) {
    union { double d; unsigned long long u; } v;
    int adj = 0;

    if (x == 0.0 || x != x || __builtin_isinf(x)) { *e = 0; return x; }
    v.d = x;
    int ex = (int)((v.u >> 52) & 0x7ff);
    if (ex == 0) {                                  // subnormal: scale it up first
        v.d = x * 18446744073709551616.0;
        adj = -64;
        ex = (int)((v.u >> 52) & 0x7ff);
    }
    *e = ex - 1022 + adj;
    v.u = (v.u & ~(0x7ffULL << 52)) | (1022ULL << 52);
    return v.d;
}

double modf(double x, double *ip) {
    if (__builtin_isinf(x)) { *ip = x; return copysign(0.0, x); }
    double t = trunc(x);
    *ip = t;
    return copysign(x - t, x);
}

// ---------- roots ---------- //

double sqrt(double x) {
    if (x < 0.0) { errno = EDOM; return NANV; }
    return (double)x_sqrt(x);
}

long double sqrtl(long double x) {
    if (x < 0.0L) { errno = EDOM; return __builtin_nanl(""); }
    return x_sqrt(x);
}

double cbrt(double x) {
    if (x == 0.0 || x != x || __builtin_isinf(x)) return x;

    long double a = x < 0 ? -(long double)x : (long double)x;
    long double r = exp2l_(x_fyl2x(a, 1.0L) / 3.0L);
    r = r - (r * r * r - a) / (3.0L * r * r);                 // one Newton step
    return (double)(x < 0 ? -r : r);
}

double hypot(double x, double y) {
    if (__builtin_isinf(x) || __builtin_isinf(y)) return INF;
    if (x != x || y != y) return NANV;

    long double a = x, b = y;
    return (double)x_sqrt(a * a + b * b);
}

// ---------- trigonometry ---------- //

static long double reduce(double x) {
    long double v = x;
    if (v > 4.0e18L || v < -4.0e18L) v = x_fprem(v, 2.0L * PIL);    // fsin needs |v| < 2^63
    return v;
}

double sin(double x) {
    if (x != x || __builtin_isinf(x)) { errno = EDOM; return NANV; }
    return (double)x_sin(reduce(x));
}

double cos(double x) {
    if (x != x || __builtin_isinf(x)) { errno = EDOM; return NANV; }
    return (double)x_cos(reduce(x));
}

double tan(double x) {
    if (x != x || __builtin_isinf(x)) { errno = EDOM; return NANV; }
    return (double)x_tan(reduce(x));
}

double atan2(double y, double x) { return (double)x_fpatan(y, x); }
double atan(double x)            { return (double)x_fpatan(x, 1.0L); }

double asin(double x) {
    if (!(x >= -1.0 && x <= 1.0)) { errno = EDOM; return NANV; }
    long double l = x;
    return (double)x_fpatan(l, x_sqrt((1.0L - l) * (1.0L + l)));
}

double acos(double x) {
    if (!(x >= -1.0 && x <= 1.0)) { errno = EDOM; return NANV; }
    long double l = x;
    return (double)x_fpatan(x_sqrt((1.0L - l) * (1.0L + l)), l);
}

// ---------- exponentials and logarithms ---------- //

double exp(double x)   { return (double)exp2l_((long double)x * LOG2E); }
double exp2(double x)  { return (double)exp2l_(x); }
double expm1(double x) { return (double)expm1l_(x); }

double log(double x) {
    if (x < 0.0)       errno = EDOM;
    else if (x == 0.0) errno = ERANGE;
    return (double)x_fyl2x(x, LN2);
}

double log2(double x) {
    if (x < 0.0)       errno = EDOM;
    else if (x == 0.0) errno = ERANGE;
    return (double)x_fyl2x(x, 1.0L);
}

double log10(double x) {
    if (x < 0.0)       errno = EDOM;
    else if (x == 0.0) errno = ERANGE;
    return (double)x_fyl2x(x, LOG10_2);
}

double log1p(double x) {
    if (x < -1.0)  { errno = EDOM;   return NANV; }
    if (x == -1.0) { errno = ERANGE; return -INF; }
    return (double)log1pl_(x);
}

double pow(double x, double y) {
    if (y == 0.0 || x == 1.0) return 1.0;
    if (x != x || y != y) return NANV;

    int yint = is_integer(y);
    int yodd = is_odd(y);
    double ax = fabs(x);

    if (x == 0.0) {
        if (y < 0.0) { errno = ERANGE; return yodd ? copysign(INF, x) : INF; }
        return yodd ? x : 0.0;
    }
    if (__builtin_isinf(y)) {
        if (ax == 1.0) return 1.0;
        return ((ax < 1.0) == (y < 0.0)) ? INF : 0.0;
    }
    if (__builtin_isinf(x)) {
        if (x > 0.0) return y < 0.0 ? 0.0 : INF;
        if (y < 0.0) return yodd ? -0.0 : 0.0;
        return yodd ? -INF : INF;
    }
    if (x < 0.0 && !yint) { errno = EDOM; return NANV; }

    long double r;
    if (yint && fabs(y) <= 1024.0) {                          // small integer power: exact by squaring
        long double b = x, p = 1.0L;
        unsigned n = (unsigned)fabs(y);

        while (n) {
            if (n & 1) p *= b;
            b *= b;
            n >>= 1;
        }
        r = y < 0.0 ? 1.0L / p : p;
    } else {
        r = exp2l_(x_fyl2x((long double)ax, (long double)y));
        if (x < 0.0 && yodd) r = -r;
    }

    double d = (double)r;
    if (__builtin_isinf(d)) errno = ERANGE;
    return d;
}

// ---------- hyperbolic functions ---------- //

double sinh(double x) {
    long double a = x < 0 ? -(long double)x : (long double)x, r;

    if (a != a) return x;
    if (a < 22.0L) {
        long double e = expm1l_(a);
        r = 0.5L * (e + e / (e + 1.0L));
    } else {
        r = 0.5L * exp2l_(a * LOG2E);
    }
    return (double)(x < 0 ? -r : r);
}

double cosh(double x) {
    long double a = x < 0 ? -(long double)x : (long double)x;

    if (a != a) return x;
    long double e = exp2l_(a * LOG2E);
    return (double)(0.5L * (e + 1.0L / e));
}

double tanh(double x) {
    long double a = x < 0 ? -(long double)x : (long double)x;

    if (a != a) return x;
    if (a > 22.0L) return x < 0 ? -1.0 : 1.0;

    long double e = expm1l_(2.0L * a);
    long double r = e / (e + 2.0L);
    return (double)(x < 0 ? -r : r);
}

double asinh(double x) {
    long double a = x < 0 ? -(long double)x : (long double)x, r;

    if (a != a || __builtin_isinf(x)) return x;
    if (a > 1e18L) r = logl_(a) + LN2;
    else           r = log1pl_(a + a * a / (1.0L + x_sqrt(1.0L + a * a)));
    return (double)(x < 0 ? -r : r);
}

double acosh(double x) {
    if (x < 1.0) { errno = EDOM; return NANV; }
    if (x != x || __builtin_isinf(x)) return x;

    long double t = (long double)x - 1.0L, r;
    if (x > 1e18) r = logl_(x) + LN2;
    else          r = log1pl_(t + x_sqrt(t * (t + 2.0L)));
    return (double)r;
}

double atanh(double x) {
    double ax = fabs(x);

    if (x != x) return x;
    if (ax > 1.0)  { errno = EDOM;   return NANV; }
    if (ax == 1.0) { errno = ERANGE; return copysign(INF, x); }

    long double a = ax;
    long double r = 0.5L * log1pl_(2.0L * a / (1.0L - a));
    return (double)(x < 0 ? -r : r);
}

// ---------- single and extended precision wrappers ---------- //

float sqrtf(float x)             { return (float)sqrt(x); }
float sinf(float x)              { return (float)sin(x); }
float cosf(float x)              { return (float)cos(x); }
float tanf(float x)              { return (float)tan(x); }
float asinf(float x)             { return (float)asin(x); }
float acosf(float x)             { return (float)acos(x); }
float atanf(float x)             { return (float)atan(x); }
float atan2f(float y, float x)   { return (float)atan2(y, x); }
float expf(float x)              { return (float)exp(x); }
float logf(float x)              { return (float)log(x); }
float log2f(float x)             { return (float)log2(x); }
float log10f(float x)            { return (float)log10(x); }
float powf(float x, float y)     { return (float)pow(x, y); }
float hypotf(float x, float y)   { return (float)hypot(x, y); }
float floorf(float x)            { return (float)floor(x); }
float ceilf(float x)             { return (float)ceil(x); }
float truncf(float x)            { return (float)trunc(x); }
float roundf(float x)            { return (float)round(x); }
float fmodf(float x, float y)    { return (float)fmod(x, y); }