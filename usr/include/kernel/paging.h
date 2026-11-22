#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define PTE_PRESENT 0x1
#define PTE_WRITE   0x2
#define PTE_USER    0x4

void     paging_init(void);

// Operate on the active address space
int      paging_map(uint32_t virt, uint32_t phys, uint32_t flags);   // 0 = ok
void     paging_unmap(uint32_t virt);
uint32_t paging_get_phys(uint32_t virt);                             // 0 = not mapped
int      paging_check_user(uint32_t virt, uint32_t len, int write);  // 1 = whole range user-accessible

// Operate on any address space (dir = physical address of its page directory)
int      paging_map_in(uint32_t dir, uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_get_phys_in(uint32_t dir, uint32_t virt);

uint32_t paging_create_space(void);               // new space: kernel shared, user part empty
uint32_t paging_clone_space(uint32_t src);        // copies every user page
void     paging_destroy_space(uint32_t dir);      // frees user pages, tables and the directory
void     paging_switch(uint32_t dir);
uint32_t paging_kernel_dir(void);

#endif