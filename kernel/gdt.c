#include <stdint.h>
#include <string.h>
#include "gdt.h"
#include "kprintf.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss_entry) == 104, "tss layout");

#define GDT_ENTRIES 6

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdtp;
static struct tss_entry tss;
static uint8_t kernel_stack[8192] __attribute__((aligned(16)));

extern void gdt_flush(uint32_t gdt_ptr_addr);

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran) {
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;

    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access      = access;
}

void gdt_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint32_t)(uintptr_t)&gdt;

    memset(&tss, 0, sizeof(tss));
    tss.ss0        = 0x10;                                        // kernel data selector
    tss.esp0       = (uint32_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack));
    tss.iomap_base = sizeof(tss);                                 // no I/O bitmap: ring 3 gets no port access

    gdt_set(0, 0, 0,          0x00, 0x00);  // null descriptor
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);  // kernel code, 0x08
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);  // kernel data, 0x10
    gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);  // user code,   0x18 (use 0x1B)
    gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);  // user data,   0x20 (use 0x23)
    gdt_set(5, (uint32_t)(uintptr_t)&tss, sizeof(tss) - 1, 0x89, 0x00);  // TSS, 0x28

    gdt_flush((uint32_t)(uintptr_t)&gdtp);
    __asm__ volatile ("ltr %%ax" : : "a"((uint16_t)0x28));
}

void gdt_debug(void) {
    const uint32_t *g = (const uint32_t *)(uintptr_t)gdt;
    for (int i = 0; i < GDT_ENTRIES; i++)
        kprintf("gdt[%d] = %08x %08x\n", i, g[i * 2 + 1], g[i * 2]);
    kprintf("tss: ss0=%x esp0=%08x\n", tss.ss0, tss.esp0);
}