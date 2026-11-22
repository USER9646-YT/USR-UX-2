#ifndef KERNEL_FB_H
#define KERNEL_FB_H

#include <stdint.h>
#include "multiboot.h"

struct fb_info {
    int      present;
    uint32_t phys;               // physical address of the framebuffer
    uint32_t width, height, pitch;
    uint32_t bpp;
    uint8_t  red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
    uint32_t size;               // pitch * height
};

int  fb_init(const struct multiboot_info *mbi);   // 1 if GRUB gave us a linear framebuffer
void fb_remap(void);                              // call right after paging_init()
const struct fb_info *fb_get(void);
uint8_t *fb_base(void);                           // where the kernel can write pixels
uint32_t fb_color(uint8_t r, uint8_t g, uint8_t b);   // packs a colour for this pixel format

#endif