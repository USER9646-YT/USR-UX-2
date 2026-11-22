#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

#ifdef __TINYC__
// tcc recognises the name alloca itself (and libtcc1 provides a fallback)
void *alloca(size_t);
#else
// gcc allocates on the stack frame itself, so this has to be a builtin, not a function
#define alloca(size) __builtin_alloca(size)
#endif

#endif