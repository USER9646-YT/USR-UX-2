#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stat.h>
#include "internal.h"

// ---------- numbers ---------- //

int abs(int x)               { return x < 0 ? -x : x; }
long labs(long x)            { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int a, int b)                  { div_t r = { a / b, a % b }; return r; }
ldiv_t ldiv(long a, long b)              { ldiv_t r = { a / b, a % b }; return r; }
lldiv_t lldiv(long long a, long long b)  { lldiv_t r = { a / b, a % b }; return r; }

static unsigned long long conv(const char *s, char **end, int base, int *neg, int *overflow) {
    const char *p = s;
    unsigned long long v = 0;
    int any = 0;

    while (isspace((unsigned char)*p)) p++;
    *neg = 0;
    *overflow = 0;
    if (*p == '+') p++;
    else if (*p == '-') { *neg = 1; p++; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }
    if (base < 2 || base > 36) { if (end) *end = (char *)s; return 0; }

    unsigned long long cutoff = ULLONG_MAX / (unsigned)base;
    unsigned int cutlim = (unsigned int)(ULLONG_MAX % (unsigned)base);

    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9')      d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;

        any = 1;
        if (v > cutoff || (v == cutoff && (unsigned int)d > cutlim)) *overflow = 1;
        else v = v * (unsigned)base + (unsigned)d;
    }
    if (end) *end = (char *)(any ? p : s);
    return v;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = conv(s, end, base, &neg, &ovf);
    if (ovf) { errno = ERANGE; return ULLONG_MAX; }
    return neg ? -v : v;
}

long long strtoll(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = conv(s, end, base, &neg, &ovf);
    if (neg) {
        if (ovf || v > (unsigned long long)LLONG_MAX + 1) { errno = ERANGE; return LLONG_MIN; }
        return (long long)(0 - v);
    }
    if (ovf || v > (unsigned long long)LLONG_MAX) { errno = ERANGE; return LLONG_MAX; }
    return (long long)v;
}

unsigned long strtoul(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = conv(s, end, base, &neg, &ovf);
    if (ovf || v > ULONG_MAX) { errno = ERANGE; return ULONG_MAX; }
    return neg ? (unsigned long)(0 - v) : (unsigned long)v;
}

long strtol(const char *s, char **end, int base) {
    int neg, ovf;
    unsigned long long v = conv(s, end, base, &neg, &ovf);
    if (neg) {
        if (ovf || v > (unsigned long long)LONG_MAX + 1) { errno = ERANGE; return LONG_MIN; }
        return (long)(0 - v);
    }
    if (ovf || v > (unsigned long long)LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    return (long)v;
}

int atoi(const char *s)         { return (int)strtol(s, NULL, 10); }
long atol(const char *s)        { return strtol(s, NULL, 10); }
long long atoll(const char *s)  { return strtoll(s, NULL, 10); }

double strtod(const char *s, char **end) {
    const char *p = s;
    int neg = 0, digits = 0, exp10 = 0;
    long double val = 0;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }

    if (strncasecmp(p, "inf", 3) == 0) {
        p += strncasecmp(p, "infinity", 8) == 0 ? 8 : 3;
        if (end) *end = (char *)p;
        return neg ? -__builtin_huge_val() : __builtin_huge_val();
    }
    if (strncasecmp(p, "nan", 3) == 0) {
        if (end) *end = (char *)(p + 3);
        return __builtin_nan("");
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
    return neg ? -(double)val : (double)val;
}

float strtof(const char *s, char **end) { return (float)strtod(s, end); }
double atof(const char *s)              { return strtod(s, NULL); }

// ---------- random numbers ---------- //

static unsigned int rand_state = 1;

void srand(unsigned int seed) { rand_state = seed; }

int rand(void) {
    rand_state = rand_state * 1103515245u + 12345u;
    return (int)((rand_state >> 16) & 0x7FFF);
}

void srandom(unsigned int seed) { srand(seed); }
long random(void)               { return ((long)rand() << 15) ^ rand(); }

// ---------- sorting and searching ---------- //

static void swap_bytes(char *a, char *b, size_t n) {
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

static void sift(char *base, size_t root, size_t n, size_t size,
                 int (*cmp)(const void *, const void *)) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n) return;
        if (child + 1 < n && cmp(base + child * size, base + (child + 1) * size) < 0) child++;
        if (cmp(base + root * size, base + child * size) >= 0) return;
        swap_bytes(base + root * size, base + child * size, size);
        root = child;
    }
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
    char *b = base;
    if (n < 2 || size == 0) return;

    for (size_t i = n / 2; i-- > 0;) sift(b, i, n, size, cmp);          // heapsort: no recursion
    for (size_t i = n - 1; i > 0; i--) {
        swap_bytes(b, b + i * size, size);
        sift(b, 0, i, size, cmp);
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *)) {
    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *e = (const char *)base + mid * size;
        int c = cmp(key, e);
        if (c == 0) return (void *)e;
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

// ---------- environment ---------- //

static int own_env;                         // environ points to an array we allocated

static size_t env_count(void) {
    size_t n = 0;
    if (environ) while (environ[n]) n++;
    return n;
}

static int own_environ(size_t extra) {
    size_t n = env_count();
    char **e = malloc((n + extra + 1) * sizeof(char *));
    if (!e) return -1;
    for (size_t i = 0; i < n; i++) e[i] = environ[i];
    e[n] = NULL;
    if (own_env) free(environ);
    environ = e;
    own_env = 1;
    return 0;
}

static int env_find(const char *name, size_t len) {
    if (!environ) return -1;
    for (int i = 0; environ[i]; i++)
        if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') return i;
    return -1;
}

char *getenv(const char *name) {
    size_t len = strlen(name);
    int i = env_find(name, len);
    return i < 0 ? NULL : environ[i] + len + 1;
}

int setenv(const char *name, const char *value, int overwrite) {
    size_t nl = strlen(name), vl = strlen(value);

    if (nl == 0 || strchr(name, '=')) { errno = EINVAL; return -1; }

    int i = env_find(name, nl);
    if (i >= 0 && !overwrite) return 0;

    char *kv = malloc(nl + vl + 2);
    if (!kv) return -1;
    memcpy(kv, name, nl);
    kv[nl] = '=';
    memcpy(kv + nl + 1, value, vl + 1);

    if (i >= 0) {
        if (!own_env && own_environ(0) < 0) { free(kv); return -1; }
        environ[i] = kv;
        return 0;
    }
    if (own_environ(1) < 0) { free(kv); return -1; }
    size_t n = env_count();
    environ[n] = kv;
    environ[n + 1] = NULL;
    return 0;
}

int putenv(char *s) {
    char *eq = strchr(s, '=');
    if (!eq) return unsetenv(s);

    size_t nl = (size_t)(eq - s);
    int i = env_find(s, nl);
    if (own_environ(i < 0 ? 1 : 0) < 0) return -1;
    if (i >= 0) { environ[i] = s; return 0; }

    size_t n = env_count();
    environ[n] = s;
    environ[n + 1] = NULL;
    return 0;
}

int unsetenv(const char *name) {
    size_t len = strlen(name);
    int i = env_find(name, len);

    if (i < 0) return 0;
    if (own_environ(0) < 0) return -1;
    for (; environ[i]; i++) environ[i] = environ[i + 1];
    return 0;
}

// ---------- processes and files ---------- //

int system(const char *cmd) {
    if (!cmd) return 1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[4] = { "sh", "-c", (char *)cmd, NULL };
        execve("/bin/sh", argv, environ);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return status;
}

char *realpath(const char *path, char *resolved) {
    char buf[512];
    size_t len = 0;
    struct stat st;

    if (path[0] != '/') {
        if (!getcwd(buf, sizeof(buf))) return NULL;
        len = strlen(buf);
    }

    const char *p = path;
    while (*p) {                                    // collapse //, . and ..
        while (*p == '/') p++;
        if (!*p) break;
        const char *c = p;
        while (*p && *p != '/') p++;
        size_t cl = (size_t)(p - c);

        if (cl == 1 && c[0] == '.') continue;
        if (cl == 2 && c[0] == '.' && c[1] == '.') {
            while (len > 0 && buf[len - 1] != '/') len--;
            if (len > 0) len--;
            continue;
        }
        if (len + 1 + cl >= sizeof(buf)) { errno = ENAMETOOLONG; return NULL; }
        buf[len++] = '/';
        memcpy(buf + len, c, cl);
        len += cl;
    }
    if (len == 0) buf[len++] = '/';
    buf[len] = '\0';

    if (stat(buf, &st) < 0) return NULL;
    if (!resolved) resolved = malloc(len + 1);
    if (!resolved) return NULL;
    memcpy(resolved, buf, len + 1);
    return resolved;
}

int mkstemp(char *tmpl) {
    size_t n = strlen(tmpl);
    static unsigned int counter;

    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX") != 0) { errno = EINVAL; return -1; }

    for (int tries = 0; tries < 100; tries++) {
        unsigned int v = (unsigned int)getpid() * 7919u + (counter++) * 104729u;
        for (int i = 0; i < 6; i++) {
            tmpl[n - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[v % 36];
            v = v / 36 + 17;
        }
        int fd = (int)__sys_ret(__syscall3(SYS_open, (long)tmpl, 0x0002 | 0x0040 | 0x0080, 0600));
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    return -1;
}