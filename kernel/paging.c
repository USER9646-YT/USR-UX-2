#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "paging.h"
#include "pmm.h"
#include "idt.h"
#include "proc.h"
#include "kprintf.h"
#include "terminal.h"

#define USER_FIRST_PDE 256                 // 0x40000000 >> 22
#define USER_LAST_PDE  511                 // 0x7FFFFFFF >> 22
#define HEAP_START     0xE0000000u
#define HEAP_END       0xF0000000u

static uint32_t *kernel_dir;
static uint32_t *cur_dir;
static int paging_enabled;

static inline void invlpg(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

// Returns the page table covering virt in `dir`, creating it if asked to.
static uint32_t *get_table_in(uint32_t *dir, uint32_t virt, int create) {
    uint32_t di = virt >> 22;

    if (!(dir[di] & PTE_PRESENT)) {
        if (!create) return NULL;
        uint32_t phys = pmm_alloc();
        if (!phys) return NULL;
        memset((void *)phys, 0, PAGE_SIZE);   // frames are identity-mapped
        dir[di] = phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }
    return (uint32_t *)(dir[di] & ~0xFFFu);
}

int paging_map_in(uint32_t dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t *d = (uint32_t *)(uintptr_t)dir;
    if (!d) return -1;

    uint32_t *table = get_table_in(d, virt, 1);
    if (!table) return -1;

    table[(virt >> 12) & 0x3FF] = (phys & ~0xFFFu) | (flags & 0xFFF) | PTE_PRESENT;
    if (paging_enabled && d == cur_dir) invlpg(virt);
    return 0;
}

uint32_t paging_get_phys_in(uint32_t dir, uint32_t virt) {
    uint32_t *d = (uint32_t *)(uintptr_t)dir;
    if (!d) return 0;

    uint32_t *table = get_table_in(d, virt, 0);
    if (!table) return 0;

    uint32_t pte = table[(virt >> 12) & 0x3FF];
    if (!(pte & PTE_PRESENT)) return 0;
    return (pte & ~0xFFFu) | (virt & 0xFFF);
}

int paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    return paging_map_in((uint32_t)(uintptr_t)cur_dir, virt, phys, flags);
}

uint32_t paging_get_phys(uint32_t virt) {
    return paging_get_phys_in((uint32_t)(uintptr_t)cur_dir, virt);
}

void paging_unmap(uint32_t virt) {
    if (!cur_dir) return;
    uint32_t *table = get_table_in(cur_dir, virt, 0);
    if (!table) return;

    table[(virt >> 12) & 0x3FF] = 0;
    if (paging_enabled) invlpg(virt);
}

int paging_check_user(uint32_t virt, uint32_t len, int write) {
    if (len == 0) return 1;
    if (!cur_dir) return 0;

    uint32_t end = virt + len;
    if (end < virt) return 0;                       // wraps around

    uint32_t need = PTE_PRESENT | PTE_USER | (write ? PTE_WRITE : 0);

    for (uint32_t a = virt & ~0xFFFu; a < end; a += PAGE_SIZE) {
        uint32_t *table = get_table_in(cur_dir, a, 0);
        if (!table) return 0;
        if ((table[(a >> 12) & 0x3FF] & need) != need) return 0;
    }
    return 1;
}

// ---------- address spaces ---------- //

uint32_t paging_create_space(void) {
    uint32_t phys = pmm_alloc();
    if (!phys) return 0;

    uint32_t *dir = (uint32_t *)phys;
    memset(dir, 0, PAGE_SIZE);

    // Share the kernel's page tables (RAM identity map + heap); user part stays empty
    for (uint32_t i = 0; i < 1024; i++)
        if (i < USER_FIRST_PDE || i > USER_LAST_PDE)
            dir[i] = kernel_dir[i];
    return phys;
}

void paging_destroy_space(uint32_t dir) {
    uint32_t *d = (uint32_t *)(uintptr_t)dir;
    if (!d) return;
    if (d == cur_dir) {
        kprintf("paging: refusing to destroy the active address space\n");
        return;
    }

    for (uint32_t di = USER_FIRST_PDE; di <= USER_LAST_PDE; di++) {
        if (!(d[di] & PTE_PRESENT)) continue;

        uint32_t *table = (uint32_t *)(uintptr_t)(d[di] & ~0xFFFu);
        for (uint32_t ti = 0; ti < 1024; ti++)
            if (table[ti] & PTE_PRESENT)
                pmm_free(table[ti] & ~0xFFFu);
        pmm_free((uint32_t)(uintptr_t)table);
    }
    pmm_free(dir);
}

uint32_t paging_clone_space(uint32_t src) {
    uint32_t *s = (uint32_t *)(uintptr_t)src;
    uint32_t dst = paging_create_space();
    if (!dst) return 0;

    for (uint32_t di = USER_FIRST_PDE; di <= USER_LAST_PDE; di++) {
        if (!(s[di] & PTE_PRESENT)) continue;
        uint32_t *table = (uint32_t *)(uintptr_t)(s[di] & ~0xFFFu);

        for (uint32_t ti = 0; ti < 1024; ti++) {
            if (!(table[ti] & PTE_PRESENT)) continue;

            uint32_t frame = pmm_alloc();
            if (!frame) { paging_destroy_space(dst); return 0; }
            memcpy((void *)frame, (void *)(uintptr_t)(table[ti] & ~0xFFFu), PAGE_SIZE);

            uint32_t virt = (di << 22) | (ti << 12);
            if (paging_map_in(dst, virt, frame, table[ti] & 0xFFF) != 0) {
                pmm_free(frame);
                paging_destroy_space(dst);
                return 0;
            }
        }
    }
    return dst;
}

void paging_switch(uint32_t dir) {
    cur_dir = (uint32_t *)(uintptr_t)dir;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(dir) : "memory");
}

uint32_t paging_kernel_dir(void) {
    return (uint32_t)(uintptr_t)kernel_dir;
}

// ---------- page faults ---------- //

static void page_fault_handler(struct registers *r) {
    uint32_t addr;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(addr));

    if (r->cs & 3) {                      // user mode: kill just that process
        kprintf("\n[pid %d: segmentation fault at %08x, EIP=%08x] killed\n",
                current->pid, addr, r->eip);
        proc_exit(139);
    }

    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\npanic: kernel page fault at %08x ***\n", addr);
    kprintf("cause: %s, %s, kernel mode\n",
            (r->err_code & 1) ? "protection violation" : "page not present",
            (r->err_code & 2) ? "write" : "read");
    kprintf("EIP=%08x ESP=%08x\n", r->eip, r->esp);
    kprintf("Looks like you fucked some shit up...\n");
    kprintf("System halted.\n");

    for (;;) __asm__ volatile ("cli; hlt");
}

void paging_init(void) {
    kernel_dir = (uint32_t *)pmm_alloc();
    memset(kernel_dir, 0, PAGE_SIZE);
    cur_dir = kernel_dir;

    // Identity-map all RAM. Frame 0 stays unmapped so NULL dereferences fault.
    uint32_t frames = pmm_total_frames();
    for (uint32_t f = 1; f < frames; f++)
        paging_map_in((uint32_t)(uintptr_t)kernel_dir, f * PAGE_SIZE, f * PAGE_SIZE, PTE_WRITE);

    // Create the heap's page tables up front, so every address space shares
    //   them and heap growth is visible everywhere.
    for (uint32_t v = HEAP_START; v < HEAP_END; v += 0x400000u)
        get_table_in(kernel_dir, v, 1);

    isr_register_handler(14, page_fault_handler);

    uint32_t cr0;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint32_t)(uintptr_t)kernel_dir));
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                           // PG bit
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    paging_enabled = 1;
}