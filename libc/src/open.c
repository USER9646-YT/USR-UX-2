#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "internal.h"

// open() that honours the mode argument. The kernel ignores it and makes every new
// file 0644, so a program that asks for an executable (tcc writes its output with
// mode 0777) would get a file it can't run. After creating a NEW file whose mode has
// an x bit, we apply the requested permissions with chmod. Plain data files keep the
// kernel's default, so fopen("w") behaves exactly as before.
int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    int existed = 1;

    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        existed = (access(path, F_OK) == 0);
    }

    int fd = (int)__sys_ret(__syscall3(SYS_open, (long)path, flags, 0));

    if (fd >= 0 && (flags & O_CREAT) && !existed && (mode & 0111)) {
        mode_t um = umask(0);                       // read the current umask ...
        umask(um);                                  // ... and put it back
        if (um == 0) um = 022;                      // the kernel doesn't apply it: use the usual default
        chmod(path, mode & ~um & 07777);
    }
    return fd;
}