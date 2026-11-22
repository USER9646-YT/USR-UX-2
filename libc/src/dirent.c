#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stat.h>
#include "internal.h"

struct _DIR {
    int           fd;
    struct dirent ent;
};

DIR *opendir(const char *path) {
    struct stat st;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode)) {
        close(fd);
        errno = ENOTDIR;
        return NULL;
    }

    DIR *d = malloc(sizeof(*d));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *d) {
    long r = __sys_ret(__syscall3(SYS_readdir, d->fd, (long)&d->ent, 0));
    return r > 0 ? &d->ent : NULL;
}

int closedir(DIR *d) {
    int r = close(d->fd);
    free(d);
    return r;
}

void rewinddir(DIR *d) {
    lseek(d->fd, 0, SEEK_SET);
}