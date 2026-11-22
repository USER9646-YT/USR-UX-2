#include <stdio.h>
#include <math.h>

static int fails;

static void check(const char *name, double got, double want) {
    double scale = fabs(want);
    if (scale == 0.0) scale = 1.0;
    int ok = fabs(got - want) <= 1e-12 * scale;

    if (!ok) fails++;
    printf("%-16s %s  got %.15g  want %.15g\n", name, ok ? "ok  " : "FAIL", got, want);
}

static void check_nan(const char *name, double got) {
    int ok = isnan(got);

    if (!ok) fails++;
    printf("%-16s %s  got %g (want nan)\n", name, ok ? "ok  " : "FAIL", got);
}

int main(void) {
    int e;
    double ip;

    check("sin(pi/6)",   sin(M_PI / 6), 0.5);
    check("cos(pi/3)",   cos(M_PI / 3), 0.5);
    check("tan(pi/4)",   tan(M_PI / 4), 1.0);
    check("asin(0.5)",   asin(0.5), M_PI / 6);
    check("acos(0.5)",   acos(0.5), M_PI / 3);
    check("atan(1)",     atan(1.0), M_PI / 4);
    check("atan2(1,-1)", atan2(1.0, -1.0), 3 * M_PI / 4);
    check("atan2(-1,-1)", atan2(-1.0, -1.0), -3 * M_PI / 4);

    check("exp(1)",      exp(1.0), M_E);
    check("exp(-1)",     exp(-1.0), 0.36787944117144233);
    check("exp2(10)",    exp2(10.0), 1024.0);
    check("exp2(-2)",    exp2(-2.0), 0.25);
    check("expm1(1e-10)", expm1(1e-10), 1.00000000005e-10);
    check("log(e)",      log(M_E), 1.0);
    check("log2(8)",     log2(8.0), 3.0);
    check("log10(1000)", log10(1000.0), 3.0);
    check("log1p(1e-10)", log1p(1e-10), 9.9999999995e-11);

    check("pow(2,10)",   pow(2.0, 10.0), 1024.0);
    check("pow(2,-1)",   pow(2.0, -1.0), 0.5);
    check("pow(2,0.5)",  pow(2.0, 0.5), 1.4142135623730951);
    check("pow(10,-2)",  pow(10.0, -2.0), 0.01);
    check("pow(-2,3)",   pow(-2.0, 3.0), -8.0);
    check_nan("pow(-8,1/3)", pow(-8.0, 1.0 / 3.0));

    check("sqrt(2)",     sqrt(2.0), 1.4142135623730951);
    check("cbrt(27)",    cbrt(27.0), 3.0);
    check("cbrt(-8)",    cbrt(-8.0), -2.0);
    check("hypot(3,4)",  hypot(3.0, 4.0), 5.0);

    check("floor(-2.5)", floor(-2.5), -3.0);
    check("ceil(-2.5)",  ceil(-2.5), -2.0);
    check("trunc(-2.7)", trunc(-2.7), -2.0);
    check("round(2.5)",  round(2.5), 3.0);
    check("round(-2.5)", round(-2.5), -3.0);
    check("round(.4999)", round(0.49999999999999994), 0.0);
    check("fmod(10,3)",  fmod(10.0, 3.0), 1.0);
    check("fmod(-10,3)", fmod(-10.0, 3.0), -1.0);
    check("fmod(5.5,2)", fmod(5.5, 2.0), 1.5);

    check("ldexp(1,10)", ldexp(1.0, 10), 1024.0);
    check("ldexp(3,-1)", ldexp(3.0, -1), 1.5);
    check("frexp(8)",    frexp(8.0, &e), 0.5);
    check("frexp exp",   e, 4.0);
    check("modf(3.75)",  modf(3.75, &ip), 0.75);
    check("modf int",    ip, 3.0);

    check("sinh(1)",     sinh(1.0), 1.1752011936438014);
    check("cosh(1)",     cosh(1.0), 1.5430806348152437);
    check("tanh(1)",     tanh(1.0), 0.7615941559557649);
    check("asinh(1)",    asinh(1.0), 0.881373587019543);
    check("acosh(2)",    acosh(2.0), 1.3169578969248166);
    check("atanh(0.5)",  atanh(0.5), 0.5493061443340548);

    printf("\n%d failure(s)\n", fails);
    return fails != 0;
}