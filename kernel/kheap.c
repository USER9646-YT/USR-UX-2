#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kheap.h"
#include "pmm.h"
#include "paging.h"
#include "kprintf.h"

#define HEAP_START   0xE0000000u
#define HEAP_MAX     0xF0000000u
#define HEAP_INIT_SZ (16 * 1024u)
#define HEAP_MIN_GROW (16 * 1024u)
#define BLOCK_MAGIC  0xC0FFEE42u
#define ALIGN16(x)   (((x) + 15u) & ~15u)

struct block {
    uint32_t      magic;
    uint32_t      size;     // payload bytes, always a multiple of 16
    uint32_t      free;
    struct block *prev;     // physically previous block, or NULL
};                          // sizeof == 16 on i386

#define HDR sizeof(struct block)

static uint32_t      heap_end = HEAP_START;   // first unmapped heap address
static struct block *last;                    // physically last block

static struct block *next_block(struct block *b) {
    uint32_t a = (uint32_t)(uintptr_t)b + HDR + b->size;
    return a < heap_end ? (struct block *)(uintptr_t)a : NULL;
}

// Map more pages at the end of the heap and turn them into free space
static int heap_grow(uint32_t bytes) {
    uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages * PAGE_SIZE < HEAP_MIN_GROW)
        pages = HEAP_MIN_GROW / PAGE_SIZE;

    uint32_t add = pages * PAGE_SIZE;
    if (heap_end + add > HEAP_MAX) return -1;

    for (uint32_t i = 0; i < pages; i++) {
        uint32_t frame = pmm_alloc();
        if (!frame || paging_map(heap_end + i * PAGE_SIZE, frame, PTE_WRITE) != 0) {
            if (frame) pmm_free(frame);
            for (uint32_t j = 0; j < i; j++) {             // roll back
                uint32_t v = heap_end + j * PAGE_SIZE;
                uint32_t p = paging_get_phys(v);
                paging_unmap(v);
                pmm_free(p);
            }
            return -1;
        }
    }

    if (last && last->free) {
        last->size += add;                  // extend the trailing free block
    } else {
        struct block *b = (struct block *)(uintptr_t)heap_end;
        b->magic = BLOCK_MAGIC;
        b->size  = add - HDR;
        b->free  = 1;
        b->prev  = last;
        last     = b;
    }
    heap_end += add;
    return 0;
}

void kheap_init(void) {
    if (heap_end != HEAP_START) return;     // already initialised

    heap_end = HEAP_START;
    last = NULL;
    if (heap_grow(HEAP_INIT_SZ) != 0)
        kprintf("kheap: initial mapping failed!\n");
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    uint32_t need = ALIGN16((uint32_t)size);

    for (;;) {
        for (struct block *b = (struct block *)HEAP_START; heap_end > HEAP_START && b; b = next_block(b)) {
            if (!b->free || b->size < need) continue;

            // Split if the leftover can hold a header plus a minimal payload
            if (b->size >= need + HDR + 16) {
                struct block *n = (struct block *)((uint8_t *)b + HDR + need);
                n->magic = BLOCK_MAGIC;
                n->size  = b->size - need - HDR;
                n->free  = 1;
                n->prev  = b;
                b->size  = need;

                struct block *after = next_block(n);
                if (after) after->prev = n;
                if (b == last) last = n;
            }
            b->free = 0;
            return (void *)(b + 1);
        }

        if (heap_grow(need + HDR) != 0) return NULL;    // out of memory
    }
}

// Merge b with its physically next block if that one is free
static void merge_with_next(struct block *b) {
    struct block *n = next_block(b);
    if (!n || !n->free) return;

    b->size += HDR + n->size;
    if (n == last) last = b;

    struct block *after = next_block(b);
    if (after) after->prev = b;
}

void kfree(void *ptr) {
    if (!ptr) return;

    struct block *b = (struct block *)ptr - 1;
    if (b->magic != BLOCK_MAGIC) {
        kprintf("kheap: kfree(%p): bad magic (corruption or bad pointer)\n", ptr);
        return;
    }
    if (b->free) {
        kprintf("kheap: double free of %p\n", ptr);
        return;
    }

    b->free = 1;
    merge_with_next(b);
    if (b->prev && b->prev->free)
        merge_with_next(b->prev);
}

void *kcalloc(size_t n, size_t size) {
    if (size && n > 0xFFFFFFFFu / size) return NULL;    // overflow
    size_t total = n * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    struct block *b = (struct block *)ptr - 1;
    if (b->magic != BLOCK_MAGIC) return NULL;
    if (size <= b->size) return ptr;                    // ITS already big enough you fuck

    void *n = kmalloc(size);
    if (!n) return NULL;
    memcpy(n, ptr, b->size);
    kfree(ptr);
    return n;
}

void kheap_dump(int verbose) {
    if (heap_end == HEAP_START) { kprintf("kheap: not initialised\n"); return; }

    uint32_t blocks = 0, used = 0, avail = 0;
    for (struct block *b = (struct block *)HEAP_START; b; b = next_block(b)) {
        blocks++;
        if (b->free) avail += b->size; else used += b->size;
        if (verbose)
            kprintf("  [%p] %s %u bytes\n", (void *)(b + 1),
                    b->free ? "free" : "used", b->size);
    }
    kprintf("kheap: %u blocks, %u used, %u free, %u KiB mapped\n",
            blocks, used, avail, (heap_end - HEAP_START) / 1024);
}