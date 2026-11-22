#ifndef _LINUX_FB_H
#define _LINUX_FB_H

#include <linux/types.h>

// ioctl requests
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOGETCMAP         0x4604
#define FBIOPUTCMAP         0x4605
#define FBIOPAN_DISPLAY     0x4606
#define FBIOBLANK           0x4611

// fb_fix_screeninfo.type
#define FB_TYPE_PACKED_PIXELS      0
#define FB_TYPE_PLANES             1
#define FB_TYPE_INTERLEAVED_PLANES 2
#define FB_TYPE_TEXT               3
#define FB_TYPE_VGA_PLANES         4

// fb_fix_screeninfo.visual
#define FB_VISUAL_MONO01             0
#define FB_VISUAL_MONO10             1
#define FB_VISUAL_TRUECOLOR          2
#define FB_VISUAL_PSEUDOCOLOR        3
#define FB_VISUAL_DIRECTCOLOR        4
#define FB_VISUAL_STATIC_PSEUDOCOLOR 5

// fb_var_screeninfo.activate
#define FB_ACTIVATE_NOW      0
#define FB_ACTIVATE_NXTOPEN  1
#define FB_ACTIVATE_TEST     2
#define FB_ACTIVATE_MASK    15
#define FB_ACTIVATE_VBL     16
#define FB_CHANGE_CMAP_VBL  32
#define FB_ACTIVATE_ALL     64
#define FB_ACTIVATE_FORCE  128
#define FB_ACTIVATE_INV_MODE 256

// fb_var_screeninfo.vmode
#define FB_VMODE_NONINTERLACED 0
#define FB_VMODE_INTERLACED    1
#define FB_VMODE_DOUBLE        2
#define FB_VMODE_MASK        255
#define FB_VMODE_YWRAP      256

// fb_var_screeninfo.rotate
#define FB_ROTATE_UR 0
#define FB_ROTATE_CW 1
#define FB_ROTATE_UD 2
#define FB_ROTATE_CCW 3

// FBIOBLANK argument
#define FB_BLANK_UNBLANK       0
#define FB_BLANK_NORMAL        1
#define FB_BLANK_VSYNC_SUSPEND 2
#define FB_BLANK_HSYNC_SUSPEND 3
#define FB_BLANK_POWERDOWN     4

#define FB_NONSTD_HAM 1

struct fb_bitfield {
    __u32 offset;               // beginning of bitfield
    __u32 length;               // length of bitfield
    __u32 msb_right;            // != 0: most significant bit is right
};

struct fb_var_screeninfo {
    __u32 xres;                 // visible resolution
    __u32 yres;
    __u32 xres_virtual;         // virtual resolution
    __u32 yres_virtual;
    __u32 xoffset;              // offset from virtual to visible
    __u32 yoffset;
    __u32 bits_per_pixel;
    __u32 grayscale;            // 0 = color, 1 = grayscale

    struct fb_bitfield red;     // bitfield in fb mem if true color
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;

    __u32 nonstd;
    __u32 activate;
    __u32 height;               // size of picture in mm
    __u32 width;
    __u32 accel_flags;

    __u32 pixclock;             // timing
    __u32 left_margin;
    __u32 right_margin;
    __u32 upper_margin;
    __u32 lower_margin;
    __u32 hsync_len;
    __u32 vsync_len;
    __u32 sync;
    __u32 vmode;
    __u32 rotate;
    __u32 colorspace;
    __u32 reserved[4];
};

struct fb_fix_screeninfo {
    char           id[16];
    unsigned long  smem_start;  // start of frame buffer memory (physical address)
    __u32          smem_len;
    __u32          type;
    __u32          type_aux;
    __u32          visual;
    __u16          xpanstep;
    __u16          ypanstep;
    __u16          ywrapstep;
    __u32          line_length; // length of a line in bytes
    unsigned long  mmio_start;
    __u32          mmio_len;
    __u32          accel;
    __u16          capabilities;
    __u16          reserved[2];
};

// colour map for palette (pseudo colour) modes: FBIOGETCMAP / FBIOPUTCMAP
struct fb_cmap {
    __u32  start;               // first entry
    __u32  len;                 // number of entries
    __u16 *red;
    __u16 *green;
    __u16 *blue;
    __u16 *transp;              // may be NULL
};

#endif