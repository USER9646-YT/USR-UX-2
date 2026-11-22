#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/time.h>

#define FD_SETSIZE 1024

typedef struct {
    unsigned int fds_bits[FD_SETSIZE / 32];
} fd_set;

#define FD_ZERO(s)      do { for (int _i = 0; _i < FD_SETSIZE / 32; _i++) (s)->fds_bits[_i] = 0; } while (0)
#define FD_SET(fd, s)   ((s)->fds_bits[(fd) / 32] |= (1u << ((fd) % 32)))
#define FD_CLR(fd, s)   ((s)->fds_bits[(fd) / 32] &= ~(1u << ((fd) % 32)))
#define FD_ISSET(fd, s) (((s)->fds_bits[(fd) / 32] >> ((fd) % 32)) & 1)

#endif /* _SYS_SELECT_H */