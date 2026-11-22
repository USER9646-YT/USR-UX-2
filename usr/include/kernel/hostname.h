#ifndef KERNEL_HOSTNAME_H
#define KERNEL_HOSTNAME_H

#include <stdint.h>

const char *hostname_get(void);
int         hostname_set(const char *name, uint32_t len);     // 0 or -errno

#endif