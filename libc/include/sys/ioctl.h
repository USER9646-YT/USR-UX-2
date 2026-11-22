#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#define TIOCSECHO  0x54F0
#define TIOCSRAW   0x54F1
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCSCTTY  0x540E
#define TIOCNOTTY  0x5422
#define FIONREAD   0x541B
#define TIOCGPTN   0x80045430
#define TIOCSPTLCK 0x40045431

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int, unsigned long, ...);
#endif