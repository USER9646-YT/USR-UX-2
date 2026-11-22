#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "pmm.h"
#include "multiboot.h"
#include "kprintf.h"

#define MAX_FRAMES (1024u * 1024u)      // 4 GiB / 4 KiB 

static uint32_t bitmap[MAX_FRAMES / 32];
static uint32_t total_frames;           // highest usable frame + 1 
static uint32_t free_frames;

extern char _kernel_start[], _kernel_end[];

static inline int is_used(uint32_t f) {
    return bitmap[f / 32] & (1u << (f % 32));
}

static void mark_used(uint32_t f) {
    if (f >= MAX_FRAMES || is_used(f)) return;
    bitmap[f / 32] |= 1u << (f % 32);
    free_frames--;
}

static void mark_free(uint32_t f) {
    if (f >= MAX_FRAMES || !is_used(f)) return;
    bitmap[f / 32] &= ~(1u << (f % 32));
    free_frames++;
}

// Mark every frame touching [start, end) as used 
static void reserve(uint32_t start, uint32_t end) {
    for (uint32_t a = start & ~(PAGE_SIZE - 1); a < end; a += PAGE_SIZE)
        mark_used(a / PAGE_SIZE);
}

void pmm_init(struct multiboot_info *mbi) {
    memset(bitmap, 0xFF, sizeof(bitmap));      // start with everything used 
    free_frames = 0;
    total_frames = 0;

    if (!(mbi->flags & MBI_FLAG_MMAP)) {
        kprintf("pmm: bootloader gave no memory map!\n");
        return;
    }

    uint32_t p   = mbi->mmap_addr;
    uint32_t end = p + mbi->mmap_length;

    while (p < end) {
        struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)p;

        if (e->type == 1) {                    // usable RAM 
            uint64_t base = e->addr;
            uint64_t top  = e->addr + e->len;
            if (top > 0x100000000ULL) top = 0x100000000ULL;   // clamp to 4 GiB 

            if (base < top) {
                uint32_t first = (uint32_t)((base + PAGE_SIZE - 1) >> 12);
                uint32_t last  = (uint32_t)(top >> 12);
                for (uint32_t f = first; f < last; f++)
                    mark_free(f);
                if (last > total_frames) total_frames = last;
            }
        }
        p += e->size + 4;                      // size field excludes itself 
    }

    // Things that must never be handed out 
    reserve(0, 0x100000);                                          // first 1 MiB: BIOS, VGA, ... 
    reserve((uint32_t)(uintptr_t)_kernel_start,
            (uint32_t)(uintptr_t)_kernel_end);                     // kernel image + stack + bitmap 
    reserve((uint32_t)(uintptr_t)mbi, (uint32_t)(uintptr_t)mbi + 116);  // multiboot info 
    reserve(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);    // the memory map itself 
}

uint32_t pmm_alloc(void) {
    uint32_t words = (total_frames + 31) / 32;

    for (uint32_t w = 0; w < words; w++) {
        if (bitmap[w] == 0xFFFFFFFFu) continue;        // all 32 used 
        for (uint32_t b = 0; b < 32; b++) {
            uint32_t f = w * 32 + b;
            if (f >= total_frames) return 0;
            if (!is_used(f)) {
                mark_used(f);
                return f * PAGE_SIZE;
            }
        }
    }
    return 0;
}

void pmm_free(uint32_t addr) {
    if (addr & (PAGE_SIZE - 1)) return;                // not page aligned 
    if (addr < 0x100000) return;                       // never free low memory 
    uint32_t f = addr / PAGE_SIZE;
    if (f >= total_frames) return;
    if (!is_used(f)) {
        kprintf("pmm: double free of %08x\n", addr);
        return;
    }
    mark_free(f);
}

uint32_t pmm_free_frames(void) {
    return free_frames;
}

uint32_t pmm_total_frames(void) {
    return total_frames;
}