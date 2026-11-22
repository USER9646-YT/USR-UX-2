#include <stdint.h>
#include <string.h>
#include "usermode.h"
#include "pmm.h"
#include "paging.h"
#include "kprintf.h"

#define USER_CODE_BASE 0x40000000u
#define USER_STACK_TOP 0x80000000u

extern void enter_usermode(uint32_t eip, uint32_t esp);
extern const uint8_t user_blob_start[], user_blob_end[];

void usermode_demo(void) {
    uint32_t size = (uint32_t)(uintptr_t)(user_blob_end - user_blob_start);
    if (size > PAGE_SIZE) {
        kprintf("usermode: demo program too large\n");
        return;
    }

    uint32_t code  = pmm_alloc();
    uint32_t stack = pmm_alloc();
    if (!code || !stack) {
        kprintf("usermode: out of memory\n");
        return;
    }

    // Frames are identity-mapped for the kernel, so it can be filled directly
    memset((void *)code, 0, PAGE_SIZE);
    memcpy((void *)code, user_blob_start, size);
    memset((void *)stack, 0, PAGE_SIZE);

    paging_map(USER_CODE_BASE, code, PTE_USER);                         // read/exec only
    paging_map(USER_STACK_TOP - PAGE_SIZE, stack, PTE_USER | PTE_WRITE);

    kprintf("entering user mode...\n");
    enter_usermode(USER_CODE_BASE, USER_STACK_TOP);
}