// This file contains the main program of USR/UX.

#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"
#include "terminal.h"
#include "kprintf.h"
#include "fb.h"
#include "gdt.h"
#include "idt.h"
#include "syscall.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "kbdmenu.h"
#include "mouse.h"
#include "ata.h"
#include "part.h"
#include "vfs.h"
#include "ramfs.h"
#include "devfs.h"
#include "procfs.h"
#include "ext2.h"
#include "proc.h"
#include "mount.h"

static void halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static void panic_init_notexist(void) {
    __asm__ volatile ("cli");
    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\npanic: no valid init found or init crashed on start\n");
    kprintf("System halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

// REMOVE IF ISSUES HAPPEN
static inline uint8_t io_inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void sleepkrnl(uint32_t ms) {
    /* Enable PIT Channel 2 gate (bit 0) and keep PC speaker off (bit 1 = 0) */
    io_outb(0x61, (io_inb(0x61) & 0xFD) | 0x01);

    for (uint32_t i = 0; i < ms; i++) {
        /* Channel 2, lobyte/hibyte, Mode 0 (OUT goes low immediately) */
        io_outb(0x43, 0xB0);

        /* 1193182 Hz / 1000 = 1193 ticks (~1 ms) */
        io_outb(0x42, 1193 & 0xFF);
        io_outb(0x42, (1193 >> 8) & 0xFF);

        /* Wait until Channel 2 OUT (bit 5 of port 0x61) goes high */
        while ((io_inb(0x61) & 0x20) == 0) {
            asm volatile("pause");
        }
    }
}
// REMOVE IF ISSUES HAPPEN

static void announce(void) {
  // Display the USR/UX startup banner. (code took from Minix because why the fuck not)
  // Adapted from MINIX 3 (Copyright Vrije Universiteit, Amsterdam).
  kprintf("\nUSR/UX %s. "
// #ifdef _VCS_REVISION
//       "(" _VCS_REVISION ")"
// #endif
         "\nCopyright 2026, USER9646\n",
         OS_RELEASE);
  kprintf("USR/UX is free software licensed under the GNU GPLv2.\n"
         "USR/UX comes with ABSOLUTELY NO WARRANTY, to the extent\n"
         "permitted by applicable law.\n");
}

void kernel_main(uint32_t magic, struct multiboot_info *mbi) {
    terminal_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kprintf("Not booted by a Multiboot loader! (magic=%x)\n", magic);
        halt();
    }

    // GRUB may have switched to a graphics mode: put the console on the framebuffer 
    if (fb_init(mbi)) terminal_attach_fb();

    terminal_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("If you see this, USR/UX 2 is running! (duh!)\n");
    terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

    kprintf("magic=%x  mbi=%p  flags=%08x\n", magic, mbi, mbi->flags);
    if (mbi->flags & MBI_FLAG_MEM)
        kprintf("memory: %u KiB lower, %u KiB upper\n", mbi->mem_lower, mbi->mem_upper);

    gdt_init();
    kprintf("GDT loaded\n");
    idt_init();
    syscall_init();
    kprintf("IDT loaded\n");

    pmm_init(mbi);
    kprintf("pmm: %u free frames (%u MiB)\n", pmm_free_frames(), pmm_free_frames() / 256);
    paging_init();
    fb_remap();                  // the framebuffer lies outside RAM: map it now that paging is on 
    kprintf("paging enabled\n");
    kheap_init();

    if (fb_get()->present)
        kprintf("fb: %ux%u, %u bits per pixel, at %08x\n",
                fb_get()->width, fb_get()->height, fb_get()->bpp, fb_get()->phys);

    pic_remap();                 // IRQs -> vectors 32-47, everything masked 
    pit_init(100);
    keyboard_init();
    mouse_init();

    sleepkrnl(250);
    kprintf("Starting keyboard menu in a second...");
    sleepkrnl(1000);
    kbdmenu_run(mbi);            // keyboard layout: kbd=<name> on the command line, or a menu 

    int ndrives = ata_init();
    kprintf("ata: %d drive(s) found\n", ndrives);

    // The root filesystem lives in the first Linux partition of the disk;
    // a disk without a partition table is treated as one bare ext2 filesystem. 
    uint32_t root_lba = 0, root_sectors = 0;
    if (part_find_linux(0, &root_lba, &root_sectors) == 0)
        kprintf("root: partition at sector %u (%u MiB)\n", root_lba, root_sectors / 2048);
    else
        kprintf("root: no partition table, using the whole disk\n");

    vfs_init();
    struct vfs_node *root = ext2_mount(0, root_lba);
    if (!root) {
        kprintf("cannot mount root filesystem!\n");
        halt();
    }
    vfs_mount("/", root);
    vfs_mount("/tmp", ramfs_create_root());      // scratch space in RAM 
    vfs_mount("/dev", devfs_create());           // device nodes
    mount_record("/", 0, root_lba, root_sectors ? root_sectors : ata_get(0)->sectors);
    vfs_mount("/proc", procfs_create());         // kernel and process information 

    struct vfs_node *run = ramfs_create_root();  // init <-> telinit, runlevel: root only 
    if (run) run->mode = 040755;
    vfs_mount("/run", run);

    announce();
    kprintf("starting init...\n");
    if (proc_spawn_init("/sbin/init") != 0) {
        kprintf("cannot start /sbin/init!\n");
        panic_init_notexist();
    }
    proc_start();                                // never returns 
}