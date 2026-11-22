#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <stddef.h>
#include <sys/types.h>

typedef unsigned int   socklen_t;
typedef unsigned short sa_family_t;

#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_LOCAL    AF_UNIX
#define AF_INET     2            // defined for source compatibility; not supported
#define PF_UNSPEC   AF_UNSPEC
#define PF_UNIX     AF_UNIX
#define PF_LOCAL    AF_LOCAL
#define PF_INET     AF_INET

#define SOCK_STREAM    1
#define SOCK_DGRAM     2         // not supported yet
#define SOCK_NONBLOCK  04000
#define SOCK_CLOEXEC   02000000  // accepted, ignored

#define SOL_SOCKET     1
#define SO_DEBUG       1
#define SO_REUSEADDR   2
#define SO_TYPE        3
#define SO_ERROR       4
#define SO_DONTROUTE   5
#define SO_BROADCAST   6
#define SO_SNDBUF      7
#define SO_RCVBUF      8
#define SO_KEEPALIVE   9
#define SO_LINGER      13
#define SO_ACCEPTCONN  30

#define SOMAXCONN      128

#define MSG_OOB        0x01
#define MSG_PEEK       0x02
#define MSG_DONTWAIT   0x40
#define MSG_NOSIGNAL   0x4000

#define SHUT_RD        0
#define SHUT_WR        1
#define SHUT_RDWR      2

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __pad[126];
};

int     socket(int, int, int);
int     socketpair(int, int, int, int[2]);
int     bind(int, const struct sockaddr *, socklen_t);
int     connect(int, const struct sockaddr *, socklen_t);
int     listen(int, int);
int     accept(int, struct sockaddr *, socklen_t *);
int     accept4(int, struct sockaddr *, socklen_t *, int);
int     getsockname(int, struct sockaddr *, socklen_t *);
int     getpeername(int, struct sockaddr *, socklen_t *);
int     shutdown(int, int);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
int     setsockopt(int, int, int, const void *, socklen_t);
int     getsockopt(int, int, int, void *, socklen_t *);
#endif