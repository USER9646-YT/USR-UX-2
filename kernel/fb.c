#include <stdint.h>
#include <string.h>
#include "fb.h"
#include "paging.h"
#include "kprintf.h"

#define FB_VIRT     0xF0000000u          // above the heap (0xE0000000-0xF0000000)
#define FB_MAX      (64u * 1024u * 1024u)
#define PTE_NOCACHE 0x18                 // PWT | PCD: don't cache device memory

static struct fb_info info;
static uint8_t *base;

// Looks for "key=" on the kernel command line (the GRUB menu entry); returns the text after it
static const char *cmdline_opt(const struct multiboot_info *mbi, const char *key) {
    if (!(mbi->flags & MBI_FLAG_CMDLINE) || !mbi->cmdline) return NULL;

    const char *cmd = (const char *)(uintptr_t)mbi->cmdline;
    size_t kl = strlen(key);

    for (const char *p = cmd; *p; p++)
        if ((p == cmd || p[-1] == ' ') && strncmp(p, key, kl) == 0) return p + kl;
    return NULL;
}

static const char *parse_num(const char *s, uint8_t *out) {
    unsigned int v = 0;

    while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned int)(*s++ - '0');
    *out = (uint8_t)v;
    if (*s == ',') s++;
    return s;
}

int fb_init(const struct multiboot_info *mbi) {
    if (!(mbi->flags & MBI_FLAG_FB)) return 0;
    if (mbi->framebuffer_type != 1) return 0;                   // not a direct-colour mode
    if (mbi->framebuffer_addr_hi != 0) return 0;                // above 4 GiB: out of reach

    uint32_t bpp = mbi->framebuffer_bpp;
    if (bpp != 32 && bpp != 24 && bpp != 16) return 0;

    info.phys       = mbi->framebuffer_addr_lo;
    info.width      = mbi->framebuffer_width;
    info.height     = mbi->framebuffer_height;
    info.pitch      = mbi->framebuffer_pitch;
    info.bpp        = bpp;
    info.red_pos    = mbi->color_info[0];
    info.red_size   = mbi->color_info[1];
    info.green_pos  = mbi->color_info[2];
    info.green_size = mbi->color_info[3];
    info.blue_pos   = mbi->color_info[4];
    info.blue_size  = mbi->color_info[5];
    info.size       = info.pitch * info.height;

    // fbfmt=rpos,rsize,gpos,gsize,bpos,bsize on the GRUB line overrides what GRUB reported
    const char *fmt = cmdline_opt(mbi, "fbfmt=");
    if (fmt) {
        uint8_t v[6];
        for (int i = 0; i < 6; i++) fmt = parse_num(fmt, &v[i]);

        if (v[1] >= 1 && v[1] <= 8 && v[3] >= 1 && v[3] <= 8 && v[5] >= 1 && v[5] <= 8 &&
            v[0] < 32 && v[2] < 32 && v[4] < 32) {
            info.red_pos = v[0];   info.red_size = v[1];
            info.green_pos = v[2]; info.green_size = v[3];
            info.blue_pos = v[4];  info.blue_size = v[5];
        }
    }

    if (!info.width || !info.height || !info.size || info.size > FB_MAX) return 0;

    info.present = 1;
    base = (uint8_t *)(uintptr_t)info.phys;     // paging is still off: physical == virtual
    return 1;
}

// After paging is on only RAM is mapped, so map the framebuffer at a fixed address.
void fb_remap(void) {
    if (!info.present) return;

    uint32_t pages = (info.size + 4095) / 4096;
    for (uint32_t i = 0; i < pages; i++) {
        if (paging_map(FB_VIRT + i * 4096, info.phys + i * 4096, PTE_WRITE | PTE_NOCACHE) != 0) {
            for (;;) __asm__ volatile ("cli; hlt");     // the screen would be unusable anyway
        }
    }
    base = (uint8_t *)(uintptr_t)FB_VIRT;

    kprintf("fb: red %u/%u, green %u/%u, blue %u/%u (bit position/size)\n",
            info.red_pos, info.red_size, info.green_pos, info.green_size,
            info.blue_pos, info.blue_size);
}

const struct fb_info *fb_get(void) {
    return &info;
}

uint8_t *fb_base(void) {
    return base;
}

uint32_t fb_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t rr = (uint32_t)r >> (8 - info.red_size);
    uint32_t gg = (uint32_t)g >> (8 - info.green_size);
    uint32_t bb = (uint32_t)b >> (8 - info.blue_size);
    return (rr << info.red_pos) | (gg << info.green_pos) | (bb << info.blue_pos);
}