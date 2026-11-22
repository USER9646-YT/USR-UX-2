#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>
#include "multiboot.h"

#define PAGE_SIZE 4096u

void     pmm_init(struct multiboot_info *mbi);
uint32_t pmm_alloc(void);               // returns physical address, 0 = out of memory
void     pmm_free(uint32_t addr);
uint32_t pmm_free_frames(void);
uint32_t pmm_total_frames(void);

#endif