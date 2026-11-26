#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H
#include <limits.h>
#include <sys/types.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN    512
#endif
#define MAXHOSTNAMELEN 64
#define NOFILE        32
#define NBBY          8
#define HZ            100

#ifndef MIN
#define MIN(a, b)     ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)     ((a) > (b) ? (a) : (b))
#endif
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define powerof2(x)   ((((x) - 1) & (x)) == 0)

#endif