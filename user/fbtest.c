#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include "ulib.h"

static unsigned int pack(const struct fb_var_screeninfo *v,
                         unsigned int r, unsigned int g, unsigned int b) {
    return ((r >> (8 - v->red.length))   << v->red.offset)   |
           ((g >> (8 - v->green.length)) << v->green.offset) |
           ((b >> (8 - v->blue.length))  << v->blue.offset);
}

static void show(const char *name, unsigned int offset, unsigned int length) {
    puts(name);
    puts(" ");
    print_int((int)offset);
    puts("/");
    print_int((int)length);
    puts("   ");
}

int main(void) {
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    char c;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        eprint("fbtest", "/dev/fb0", fd);
        return 1;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, (unsigned int)&vi) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, (unsigned int)&fi) < 0) {
        puts("fbtest: ioctl failed\n");
        return 1;
    }

    puts("mode: ");
    print_int((int)vi.xres);
    puts("x");
    print_int((int)vi.yres);
    puts("x");
    print_int((int)vi.bits_per_pixel);
    puts("\nchannels (offset/length): ");
    show("red", vi.red.offset, vi.red.length);
    show("green", vi.green.offset, vi.green.length);
    show("blue", vi.blue.offset, vi.blue.length);
    puts("\n");

    if (vi.bits_per_pixel != 32) {
        puts("fbtest: needs a 32 bit mode\n");
        return 1;
    }

    unsigned char *fb = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) {
        puts("fbtest: mmap failed\n");
        return 1;
    }

    puts("press a key to show the colour bars, then a key again to leave\n");
    ioctl(0, TIOCSRAW, 1);
    read(0, &c, 1);

    ioctl(0, KDSETMODE, KD_GRAPHICS);               // the console stops drawing

    // top half: white, red, green, blue, yellow, cyan, magenta, black.
    // bottom half: a grey ramp, which must stay perfectly neutral
    unsigned int bars[8] = {
        pack(&vi, 255, 255, 255), pack(&vi, 255, 0, 0),   pack(&vi, 0, 255, 0),
        pack(&vi, 0, 0, 255),     pack(&vi, 255, 255, 0), pack(&vi, 0, 255, 255),
        pack(&vi, 255, 0, 255),   0
    };

    for (unsigned int y = 0; y < vi.yres; y++) {
        unsigned int *row = (unsigned int *)(fb + y * fi.line_length);
        for (unsigned int x = 0; x < vi.xres; x++) {
            if (y < vi.yres / 2) {
                row[x] = bars[x * 8 / vi.xres];
            } else {
                unsigned int v = x * 255 / vi.xres;
                row[x] = pack(&vi, v, v, v);
            }
        }
    }

    read(0, &c, 1);

    ioctl(0, KDSETMODE, KD_TEXT);
    ioctl(0, TIOCSRAW, 0);
    munmap(fb, fi.smem_len);
    close(fd);
    return 0;
}