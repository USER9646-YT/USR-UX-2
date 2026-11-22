#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include "fbdev.h"
#include "fb.h"
#include "vfs.h"
#include "paging.h"
#include "pmm.h"

static int fb_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    const struct fb_info *fi = fb_get();
    (void)n;

    if (off >= fi->size) return 0;
    if (len > fi->size - off) len = fi->size - off;
    memcpy(buf, fb_base() + off, len);
    return (int)len;
}

static int fb_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    const struct fb_info *fi = fb_get();
    (void)n;

    if (off >= fi->size) return -ENOSPC;
    if (len > fi->size - off) len = fi->size - off;
    memcpy(fb_base() + off, buf, len);
    return (int)len;
}

static int fb_ioctl(struct vfs_node *n, uint32_t req, uint32_t arg) {
    const struct fb_info *fi = fb_get();
    (void)n;

    switch (req) {
    case FBIOGET_VSCREENINFO:
    case FBIOPUT_VSCREENINFO: {             // the mode can't be changed: report the current one
        struct fb_var_screeninfo *v = (struct fb_var_screeninfo *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)v, sizeof(*v), 1)) return -EFAULT;

        memset(v, 0, sizeof(*v));
        v->xres           = fi->width;
        v->yres           = fi->height;
        v->xres_virtual   = fi->width;
        v->yres_virtual   = fi->height;
        v->bits_per_pixel = fi->bpp;
        v->red.offset     = fi->red_pos;    v->red.length   = fi->red_size;
        v->green.offset   = fi->green_pos;  v->green.length = fi->green_size;
        v->blue.offset    = fi->blue_pos;   v->blue.length  = fi->blue_size;
        v->height         = 0xFFFFFFFFu;    // physical size unknown
        v->width          = 0xFFFFFFFFu;
        return 0;
    }
    case FBIOGET_FSCREENINFO: {
        struct fb_fix_screeninfo *f = (struct fb_fix_screeninfo *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)f, sizeof(*f), 1)) return -EFAULT;

        memset(f, 0, sizeof(*f));
        memcpy(f->id, "usrux fb", 9);
        f->smem_start  = fi->phys;
        f->smem_len    = (fi->size + 4095) & ~4095u;
        f->type        = FB_TYPE_PACKED_PIXELS;
        f->visual      = FB_VISUAL_TRUECOLOR;
        f->line_length = fi->pitch;
        return 0;
    }
    case FBIOPAN_DISPLAY:
        return 0;
    default:
        return -EINVAL;
    }
}

static int fb_mmap(struct vfs_node *n, uint32_t offset, uint32_t len, uint32_t *phys) {
    const struct fb_info *fi = fb_get();
    uint32_t size = (fi->size + 4095) & ~4095u;
    (void)n;

    if (offset & 4095) return -EINVAL;
    if (offset >= size || len > size - offset) return -EINVAL;

    // user mappings are freed with the process; memory inside RAM would be handed
    // out again by the allocator, so only memory outside RAM may be mapped
    if (fi->phys / PAGE_SIZE < pmm_total_frames()) return -ENODEV;

    *phys = fi->phys + offset;
    return 0;
}

static struct vfs_ops fb_ops = {
    .read  = fb_read,
    .write = fb_write,
    .ioctl = fb_ioctl,
    .mmap  = fb_mmap,
};

static struct vfs_node fb_node = {
    .ino  = 20,
    .type = VFS_CHAR,
    .refs = 1,                              // static: never freed
    .mode = S_IFCHR | 0666,
    .ops  = &fb_ops,
};

struct vfs_node *fbdev_node(void) {
    if (!fb_get()->present) return NULL;
    fb_node.size = fb_get()->size;
    return &fb_node;
}