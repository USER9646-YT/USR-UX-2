#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <unistd.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define MAP_FAILED ((void *)-1)

#define SYS_mmap_old 90
#define SYS_munmap   91

struct __mmap_args {
    unsigned int addr, len, prot, flags, fd, offset;
};

static inline void *mmap(void *addr, unsigned int len, int prot, int flags, int fd,
                         unsigned int offset) {
    struct __mmap_args a = { (unsigned int)addr, len, (unsigned int)prot,
                             (unsigned int)flags, (unsigned int)fd, offset };
    int r = __syscall3(SYS_mmap_old, (int)&a, 0, 0);
    return r < 0 ? MAP_FAILED : (void *)r;
}

static inline int munmap(void *addr, unsigned int len) {
    return __syscall3(SYS_munmap, (int)addr, (int)len, 0);
}

#endif /* _SYS_MMAN_H */