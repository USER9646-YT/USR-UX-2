#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "syscall.h"
#include "proc.h"
#include "paging.h"
#include "pmm.h"

#define BRK_LIMIT 0x5F000000u            // the mmap area starts at 0x60000000

// brk(addr): 45. Returns the new break, or the current one if the request can't be met;
// brk(0) just reports the current break.
static int sys_brk(struct registers *r) {
    uint32_t want = r->ebx;

    if (want == 0 || want < current->brk_start || want > BRK_LIMIT)
        return (int)current->brk_end;

    uint32_t old_top = (current->brk_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t new_top = (want + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (new_top > old_top) {                          // grow: fresh zeroed pages
        for (uint32_t a = old_top; a < new_top; a += PAGE_SIZE) {
            uint32_t frame = pmm_alloc();
            if (frame) memset((void *)frame, 0, PAGE_SIZE);

            if (!frame || paging_map(a, frame, PTE_USER | PTE_WRITE) != 0) {
                if (frame) pmm_free(frame);
                for (uint32_t b = old_top; b < a; b += PAGE_SIZE) {      // undo
                    uint32_t p = paging_get_phys(b) & ~(PAGE_SIZE - 1);
                    paging_unmap(b);
                    pmm_free(p);
                }
                return (int)current->brk_end;
            }
        }
    } else if (new_top < old_top) {                   // shrink: please give the pages back
        for (uint32_t a = new_top; a < old_top; a += PAGE_SIZE) {
            uint32_t p = paging_get_phys(a) & ~(PAGE_SIZE - 1);
            if (!p) continue;
            paging_unmap(a);
            pmm_free(p);
        }
    }

    current->brk_end = want;
    return (int)want;
}

void sys_mem_init(void) {
    syscall_register(45, sys_brk);
}