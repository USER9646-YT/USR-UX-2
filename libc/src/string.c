#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dp = d;
    const unsigned char *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--) *--dp = *--sp;
    }
    return d;
}

void *memset(void *d, int c, size_t n) {
    unsigned char *dp = d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++;
        y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}

void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) {
        if (*--p == (unsigned char)c) return (void *)p;
    }
    return NULL;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = '\0';
    return d;
}

char *stpcpy(char *d, const char *s) {
    while ((*d = *s++)) d++;
    return d;
}

char *strcat(char *d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n) {
    char *p = d + strlen(d);
    while (n-- && *s) *p++ = *s++;
    *p = '\0';
    return d;
}

size_t strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        memcpy(d, s, c);
        d[c] = '\0';
    }
    return len;
}

size_t strlcat(char *d, const char *s, size_t n) {
    size_t dl = strnlen(d, n);
    if (dl == n) return n + strlen(s);
    return dl + strlcpy(d + dl, s, n - dl);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

int strcoll(const char *a, const char *b) {
    return strcmp(a, b);
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; n--; }
    return n ? tolower((unsigned char)*a) - tolower((unsigned char)*b) : 0;
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c) {
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}

char *strstr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, nl) == 0) return (char *)h;
    return NULL;
}

char *strpbrk(const char *s, const char *set) {
    for (; *s; s++)
        if (strchr(set, *s)) return (char *)s;
    return NULL;
}

size_t strspn(const char *s, const char *set) {
    size_t n = 0;
    while (s[n] && strchr(set, s[n])) n++;
    return n;
}

size_t strcspn(const char *s, const char *set) {
    size_t n = 0;
    while (s[n] && !strchr(set, s[n])) n++;
    return n;
}

char *strtok_r(char *s, const char *delim, char **save) {
    if (!s) s = *save;
    if (!s) return NULL;
    s += strspn(s, delim);
    if (!*s) { *save = NULL; return NULL; }
    char *end = s + strcspn(s, delim);
    if (*end) { *end = '\0'; *save = end + 1; }
    else      { *save = NULL; }
    return s;
}

char *strtok(char *s, const char *delim) {
    static char *save;
    return strtok_r(s, delim, &save);
}

char *strsep(char **sp, const char *delim) {
    char *s = *sp;
    if (!s) return NULL;
    char *end = s + strcspn(s, delim);
    if (*end) { *end = '\0'; *sp = end + 1; }
    else      { *sp = NULL; }
    return s;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *strndup(const char *s, size_t max) {
    size_t n = strnlen(s, max);
    char *p = malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

char *strerror(int e) {
    static char unknown[32];

    switch (e) {
    case 0:            return "Success";
    case EPERM:        return "Operation not permitted";
    case ENOENT:       return "No such file or directory";
    case ESRCH:        return "No such process";
    case EINTR:        return "Interrupted system call";
    case EIO:          return "Input/output error";
    case ENXIO:        return "No such device or address";
    case E2BIG:        return "Argument list too long";
    case ENOEXEC:      return "Exec format error";
    case EBADF:        return "Bad file descriptor";
    case ECHILD:       return "No child processes";
    case EAGAIN:       return "Resource temporarily unavailable";
    case ENOMEM:       return "Cannot allocate memory";
    case EACCES:       return "Permission denied";
    case EFAULT:       return "Bad address";
    case EBUSY:        return "Device or resource busy";
    case EEXIST:       return "File exists";
    case EXDEV:        return "Invalid cross-device link";
    case ENODEV:       return "No such device";
    case ENOTDIR:      return "Not a directory";
    case EISDIR:       return "Is a directory";
    case EINVAL:       return "Invalid argument";
    case ENFILE:       return "Too many open files in system";
    case EMFILE:       return "Too many open files";
    case ENOTTY:       return "Inappropriate ioctl for device";
    case EFBIG:        return "File too large";
    case ENOSPC:       return "No space left on device";
    case ESPIPE:       return "Illegal seek";
    case EROFS:        return "Read-only file system";
    case EMLINK:       return "Too many links";
    case EPIPE:        return "Broken pipe";
    case EDOM:         return "Numerical argument out of domain";
    case ERANGE:       return "Numerical result out of range";
    case ENAMETOOLONG: return "File name too long";
    case ENOSYS:       return "Function not implemented";
    case ENOTEMPTY:    return "Directory not empty";
    case ELOOP:        return "Too many levels of symbolic links";
    case EILSEQ:       return "Invalid or incomplete multibyte or wide character";
    case ENOTSUP:      return "Operation not supported";
    default: {
        char tmp[16];
        int n = 0;
        unsigned int v = e < 0 ? (unsigned int)-e : (unsigned int)e;
        do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        strcpy(unknown, "Unknown error ");
        char *p = unknown + 14;
        if (e < 0) *p++ = '-';
        while (n) *p++ = tmp[--n];
        *p = '\0';
        return unknown;
    }
    }
}