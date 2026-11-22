#include <stdint.h>
#include <stddef.h>
#include "terminal.h"
#include "kprintf.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "multiboot.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "string.h"
#include "ata.h"
#include <errno.h>
#include <fcntl.h>
#include "vfs.h"
#include "ramfs.h"
#include "ext2.h"
#include "syscall.h"
#include "usermode.h"
#include "elf.h"
#include "proc.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEM    ((volatile uint16_t *)0xB8000)

static size_t row, col;
static const uint8_t color = 0x0F; // White on black text

void kpanic(void) {
    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\npanic: unknown error.\n");
    kprintf("error might be specified above this message.\n");
    kprintf("System halted.\n");
    for (;;) __asm__ volatile ("hlt");
}

void clear(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEM[i] = (uint16_t)' ' | (uint16_t)color << 8;
    row = col = 0;
}

void putc(char c) {
    if (c == '\n') { col = 0; row++; return; }
    VGA_MEM[row * VGA_WIDTH + col] = (uint16_t)c | (uint16_t)color << 8;
    if (++col == VGA_WIDTH) { col = 0; row++; }
}

void puts(const char *s) {
    while (*s) putc(*s++);
}

static void bp_handler(struct registers *r) {
    kprintf("breakpoint hit at EIP=%08x\n", r->eip);
}

void kernel_main(uint32_t magic, struct multiboot_info *mbi) {
    terminal_init();

    if (magic != 0x2BADB002) {
        kprintf("Not booted by a Multiboot loader! (magic=%x)\n", magic);
        return;
    }

    terminal_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("USR/UX 2 0.20 is running...\n");
    terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

    kprintf("magic=%x  mbi=%p  flags=%08x\n", magic, mbi, mbi->flags);

    if (mbi->flags & 1)
        kprintf("memory: %u KiB lower, %u KiB upper\n",
                mbi->mem_lower, mbi->mem_upper);

    kprintf("tests: [%5d] [%-3s] [%05d] [%c] [%X] [%d]\n",
            -42, "ab", 123, 'Z', 0xBEEF, -2147483647 - 1);

    gdt_init();
    kprintf("GDT loaded\n");
    idt_init();
    kprintf("IDT loaded\n");
    syscall_init();
    pmm_init(mbi);
    kprintf("pmm: %u free frames (%u MiB)\n",
            pmm_free_frames(), pmm_free_frames() / 256);

    paging_init();
    kprintf("paging enabled\n");
    kheap_init();

    pic_remap();            // IRQ 0-15 -> vectors 32-47, everything masked
    pit_init(100);          // unmasks IRQ 0
    keyboard_init();        // unmasks IRQ 1

    int ndrives = ata_init();
    kprintf("ata: %d drive(s) found\n", ndrives);

    if (ndrives > 0) {
        uint8_t *sec = kmalloc(ATA_SECTOR_SIZE);
        if (ata_read(0, 1, 1, sec) == 0) {
            sec[ATA_SECTOR_SIZE - 1] = 0;
        } else {
            kprintf("ata: read failed\n");
        }
        kfree(sec);
    }

    vfs_init();
    struct vfs_node *root = ext2_mount(0, 0);
    if (!root) {
        kprintf("cannot mount root filesystem!\n");
        kpanic();
    }
    vfs_mount("/", root);
    vfs_mount("/tmp", ramfs_create_root());

    kprintf("starting init...\n");
    if (proc_spawn_init("/sbin/init") != 0) {
        kprintf("cannot start /sbin/init!\n");
        kpanic();
    }
    proc_start();   
}