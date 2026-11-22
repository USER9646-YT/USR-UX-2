#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

long __syscall6(long, long, long, long, long, long, long);   // asm.s

#define SYS_SOCKETCALL 102

#define SC_SOCKET      1
#define SC_BIND        2
#define SC_CONNECT     3
#define SC_LISTEN      4
#define SC_ACCEPT      5
#define SC_GETSOCKNAME 6
#define SC_GETPEERNAME 7
#define SC_SOCKETPAIR  8
#define SC_SEND        9
#define SC_RECV        10
#define SC_SENDTO      11
#define SC_RECVFROM    12
#define SC_SHUTDOWN    13
#define SC_SETSOCKOPT  14
#define SC_GETSOCKOPT  15
#define SC_ACCEPT4     18

#define P(x) ((long)(x))

// the kernel's socketcall(call, args[]) takes the arguments as an array of words
static int sockcall(int call, long a0, long a1, long a2, long a3, long a4, long a5) {
    long args[6];
    long r;

    args[0] = a0; args[1] = a1; args[2] = a2;
    args[3] = a3; args[4] = a4; args[5] = a5;

    r = __syscall6(SYS_SOCKETCALL, call, P(args), 0, 0, 0, 0);
    if ((unsigned long)r > (unsigned long)-4096L) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int socket(int domain, int type, int protocol) {
    return sockcall(SC_SOCKET, domain, type, protocol, 0, 0, 0);
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    return sockcall(SC_SOCKETPAIR, domain, type, protocol, P(sv), 0, 0);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    return sockcall(SC_BIND, fd, P(addr), len, 0, 0, 0);
}

int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    return sockcall(SC_CONNECT, fd, P(addr), len, 0, 0, 0);
}

int listen(int fd, int backlog) {
    return sockcall(SC_LISTEN, fd, backlog, 0, 0, 0, 0);
}

int accept(int fd, struct sockaddr *addr, socklen_t *len) {
    return sockcall(SC_ACCEPT, fd, P(addr), P(len), 0, 0, 0);
}

int accept4(int fd, struct sockaddr *addr, socklen_t *len, int flags) {
    return sockcall(SC_ACCEPT4, fd, P(addr), P(len), flags, 0, 0);
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    return sockcall(SC_GETSOCKNAME, fd, P(addr), P(len), 0, 0, 0);
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *len) {
    return sockcall(SC_GETPEERNAME, fd, P(addr), P(len), 0, 0, 0);
}

int shutdown(int fd, int how) {
    return sockcall(SC_SHUTDOWN, fd, how, 0, 0, 0, 0);
}

ssize_t send(int fd, const void *buf, size_t len, int flags) {
    return sockcall(SC_SEND, fd, P(buf), (long)len, flags, 0, 0);
}

ssize_t recv(int fd, void *buf, size_t len, int flags) {
    return sockcall(SC_RECV, fd, P(buf), (long)len, flags, 0, 0);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t alen) {
    return sockcall(SC_SENDTO, fd, P(buf), (long)len, flags, P(addr), alen);
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *addr, socklen_t *alen) {
    return sockcall(SC_RECVFROM, fd, P(buf), (long)len, flags, P(addr), P(alen));
}

int setsockopt(int fd, int level, int opt, const void *val, socklen_t len) {
    return sockcall(SC_SETSOCKOPT, fd, level, opt, P(val), len, 0);
}

int getsockopt(int fd, int level, int opt, void *val, socklen_t *len) {
    return sockcall(SC_GETSOCKOPT, fd, level, opt, P(val), P(len), 0);
}