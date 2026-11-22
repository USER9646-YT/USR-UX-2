#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#define TIOCSECHO  0x54F0
#define TIOCSRAW   0x54F1
#define TIOCGWINSZ 0x5413
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int, unsigned long, ...);
#endif