#ifndef _MATH_H
#define _MATH_H

#define M_E        2.7182818284590452354
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

#define HUGE_VAL  (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define INFINITY  (__builtin_inff())
#define NAN       (__builtin_nanf(""))

#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x)  __builtin_signbit(x)

double fabs(double);   double sqrt(double);   double cbrt(double);
double sin(double);    double cos(double);    double tan(double);
double asin(double);   double acos(double);   double atan(double);  double atan2(double, double);
double sinh(double);   double cosh(double);   double tanh(double);
double asinh(double);  double acosh(double);  double atanh(double);
double exp(double);    double exp2(double);   double expm1(double);
double log(double);    double log2(double);   double log10(double);  double log1p(double);
double pow(double, double);   double hypot(double, double);
double floor(double);  double ceil(double);   double trunc(double);  double round(double);
double rint(double);   double nearbyint(double);
double fmod(double, double);  double fmin(double, double);  double fmax(double, double);
double copysign(double, double);  double ldexp(double, int);
double frexp(double, int *);      double modf(double, double *);
long   lround(double);  long lrint(double);

float fabsf(float);  float sqrtf(float);  float sinf(float);  float cosf(float);  float tanf(float);
float atanf(float);  float atan2f(float, float);  float expf(float);  float logf(float);
float powf(float, float);  float floorf(float);  float ceilf(float);  float fmodf(float, float);
float roundf(float);  float truncf(float);  float log10f(float);  float log2f(float);
float asinf(float);  float acosf(float);  float hypotf(float, float);
float ldexpf(float, int);
long double fabsl(long double);  long double sqrtl(long double);
long double ldexpl(long double, int);
#endif