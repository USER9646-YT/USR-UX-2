#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MBI_FLAG_MEM     (1u << 0)
#define MBI_FLAG_CMDLINE (1u << 2)
#define MBI_FLAG_MMAP    (1u << 6)
#define MBI_FLAG_FB      (1u << 12)

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint32_t framebuffer_addr_lo;
    uint32_t framebuffer_addr_hi;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;          // 1 = direct RGB, 2 = EGA text
    uint8_t  color_info[6];             // type 1: red pos/size, green pos/size, blue pos/size
} __attribute__((packed));

_Static_assert(sizeof(struct multiboot_info) == 116, "multiboot_info layout");

struct multiboot_mmap_entry {
    uint32_t size;      // size of the rest of this entry, NOT including this field
    uint64_t addr;
    uint64_t len;
    uint32_t type;      // 1 = usable RAM, everything else = reserved
} __attribute__((packed));

#endif