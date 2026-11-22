#ifndef KERNEL_USOCK_H
#define KERNEL_USOCK_H

#include <stdint.h>

#ifndef AF_UNIX
#define AF_UNIX        1
#define AF_LOCAL       AF_UNIX
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#endif
#define SOCK_NONBLOCK  04000          // same value as Linux i386
#define SOCK_CLOEXEC   02000000

#define SOL_SOCKET     1
#define SO_TYPE        3
#define SO_ERROR       4
#define SO_SNDBUF      7
#define SO_RCVBUF      8
#define SO_ACCEPTCONN  30

#define MSG_PEEK       0x02
#define MSG_DONTWAIT   0x40

#define SHUT_RD        0
#define SHUT_WR        1
#define SHUT_RDWR      2

// registers the socketcall syscall (102); call it from syscall_register_all()
void sys_socket_init(void);

#endif