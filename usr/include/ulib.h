#ifndef ULIB_H
#define ULIB_H

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>

static inline unsigned int ustrlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static inline int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static inline void puts(const char *s) {
    write(1, s, ustrlen(s));
}

static inline void print_int(int v) {
    char buf[12];
    int i = 11;
    unsigned int u = v < 0 ? -(unsigned int)v : (unsigned int)v;

    buf[i] = '\0';
    do { buf[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) buf[--i] = '-';
    puts(buf + i);
}

static inline const char *errstr(int e) {
    switch (-e) {
    case 1:  return "operation not permitted";
    case 2:  return "no such file or directory";
    case 3:  return "no such process";
    case 4:  return "interrupted";
    case 11: return "try again";
    case 13: return "permission denied";
    case 16: return "device or resource busy";
    case 17: return "file exists";
    case 18: return "cross-device link";
    case 20: return "not a directory";
    case 21: return "is a directory";
    case 22: return "invalid argument";
    case 24: return "too many open files";
    case 27: return "file too large";
    case 28: return "no space left on device";
    case 30: return "read-only file system";
    case 32: return "broken pipe";
    case 36: return "name too long";
    case 39: return "directory not empty";
    default: return "error";
    }
}

/* prints "prog: what: message" */
static inline void eprint(const char *prog, const char *what, int e) {
    puts(prog);
    puts(": ");
    puts(what);
    puts(": ");
    puts(errstr(e));
    puts("\n");
}

/* ---- event waiting (a real libc will provide these later) ---- */

static inline int __syscall5(int n, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                      : "memory");
    return ret;
}

static inline int fcntl(int fd, int cmd, int arg) {
    return __syscall3(55, fd, cmd, arg);
}

static inline int poll(struct pollfd *fds, unsigned int nfds, int timeout_ms) {
    return __syscall3(168, (int)fds, (int)nfds, timeout_ms);
}

static inline int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv) {
    return __syscall5(142, n, (int)r, (int)w, (int)e, (int)tv);
}

#endif