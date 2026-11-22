#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
#include <alloca.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX     32767
#define MB_CUR_MAX   1

typedef struct { int quot, rem; }           div_t;
typedef struct { long quot, rem; }          ldiv_t;
typedef struct { long long quot, rem; }     lldiv_t;

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void  free(void *);

void  exit(int) __attribute__((noreturn));
void  _Exit(int) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));
int   atexit(void (*)(void));
int   system(const char *);

char *getenv(const char *);
int   setenv(const char *, const char *, int);
int   unsetenv(const char *);
int   putenv(char *);
extern char **environ;

int       abs(int);
long      labs(long);
long long llabs(long long);
div_t     div(int, int);
ldiv_t    ldiv(long, long);
lldiv_t   lldiv(long long, long long);

int       atoi(const char *);
long      atol(const char *);
long long atoll(const char *);
double    atof(const char *);
long               strtol(const char *, char **, int);
unsigned long      strtoul(const char *, char **, int);
long long          strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double             strtod(const char *, char **);
float              strtof(const char *, char **);
long double        strtold(const char *, char **);

int  rand(void);
void srand(unsigned int);
long random(void);
void srandom(unsigned int);

void  qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));

char *realpath(const char *, char *);
int   mkstemp(char *);

int   posix_openpt(int);
int   grantpt(int);
int   unlockpt(int);
char *ptsname(int);
int   ptsname_r(int, char *, size_t);
#endif