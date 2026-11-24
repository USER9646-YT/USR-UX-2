#include <errno.h>
#include <sys/mount.h>

long __syscall6(long, long, long, long, long, long, long);   // asm.s

static int ret(long r) {
    if ((unsigned long)r > (unsigned long)-4096L) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int mount(const char *source, const char *target, const char *fstype,
          unsigned long flags, const void *data) {
    return ret(__syscall6(21, (long)source, (long)target, (long)fstype, (long)flags, (long)data, 0));
}

int umount(const char *target) {
    return ret(__syscall6(22, (long)target, 0, 0, 0, 0, 0));
}