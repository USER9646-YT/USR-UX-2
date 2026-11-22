#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

void *mmap(void *, size_t, int, int, int, off_t);
int   munmap(void *, size_t);
int   mprotect(void *, size_t, int);
int   msync(void *, size_t, int);
#endif