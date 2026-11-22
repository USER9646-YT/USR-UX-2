#include <errno.h>
#include <sys/types.h>

long __syscall6(long, long, long, long, long, long, long);   // asm.s

pid_t setsid(void) {
    long r = __syscall6(66, 0, 0, 0, 0, 0, 0);
    if ((unsigned long)r > (unsigned long)-4096L) {
        errno = (int)-r;
        return -1;
    }
    return (pid_t)r;
}