#include <errno.h>
#include <sys/time.h>

long __syscall6(long, long, long, long, long, long, long);   // asm.s

// Linux i386 number 79. Needs the kernel side; without it this fails with ENOSYS.
int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    long r = __syscall6(79, (long)tv, (long)tz, 0, 0, 0, 0);
    if ((unsigned long)r > (unsigned long)-4096L) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}