#ifndef _STRINGS_H
#define _STRINGS_H
#include <string.h>
#define bzero(p, n)      memset((p), 0, (n))
#define bcopy(s, d, n)   memmove((d), (s), (n))
#endif