#ifndef KERNEL_PIT_H
#define KERNEL_PIT_H

#include <stdint.h>

void     pit_init(uint32_t hz);
uint32_t pit_ticks(void);
uint32_t pit_freq(void);

#endif