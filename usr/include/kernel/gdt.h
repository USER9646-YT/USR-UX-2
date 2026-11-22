#ifndef KERNEL_GDT_H
#define KERNEL_GDT_H

#include <stdint.h>

void gdt_init(void);
void gdt_set_kernel_stack(uint32_t esp0);   // stack used when ring 3 enters the kernel
void gdt_debug(void);

#endif