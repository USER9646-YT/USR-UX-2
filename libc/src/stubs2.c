#include <errno.h>
#include <sys/file.h>
#include <utime.h>

#define WEAK __attribute__((weak))

WEAK int flock(int fd, int op) {
    (void)op;
    if (fd < 0) { errno = EBADF; return -1; }
    return 0;
}

WEAK int utime(const char *path, const struct utimbuf *times) {
    (void)path;
    (void)times;
    errno = ENOSYS;                 // phase 2: needs file times in the VFS
    return -1;
}