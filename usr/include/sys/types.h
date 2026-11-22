#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#include <stddef.h>
typedef int           pid_t;
typedef unsigned int  uid_t;
typedef unsigned int  gid_t;
typedef long          off_t;
typedef int           ssize_t;
typedef unsigned int  mode_t;
typedef unsigned int  ino_t;
typedef unsigned int  dev_t;
typedef unsigned int  nlink_t;
typedef long          blksize_t;
typedef long          blkcnt_t;
typedef long          time_t;
typedef long          suseconds_t;
typedef unsigned int  useconds_t;
typedef long          clock_t;
#endif