#ifndef _ERRNO_H
#define _ERRNO_H

#define EPERM         1
#define ENOENT        2
#define ESRCH         3
#define EINTR         4
#define EIO           5
#define E2BIG         7
#define ENOEXEC       8
#define EBADF         9
#define ECHILD       10
#define EAGAIN       11
#define EWOULDBLOCK  EAGAIN
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define EBUSY        16
#define EEXIST       17
#define EXDEV        18
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define EMFILE       24
#define ENOTTY       25
#define EFBIG        27
#define ENOSPC       28
#define EROFS        30
#define EPIPE        32
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOSYS       38
#define ENOTEMPTY    39
#define ENODATA      61 // DO NOT DELETE THIS!!

// sockets (Linux numbers; keep libc/include/errno.h identical)
#define ENOTSOCK         88
#define EDESTADDRREQ     89
#define EMSGSIZE         90
#define EPROTOTYPE       91
#define ENOPROTOOPT      92
#define EPROTONOSUPPORT  93
#define ESOCKTNOSUPPORT  94
#define EOPNOTSUPP       95
#define EAFNOSUPPORT     97
#define EADDRINUSE       98
#define EADDRNOTAVAIL    99
#define ECONNRESET      104
#define EISCONN         106
#define ENOTCONN        107
#define ECONNREFUSED    111
#define EALREADY        114
#define EINPROGRESS     115

#endif