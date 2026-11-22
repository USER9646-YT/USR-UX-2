#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#define TIOCSECHO  0x54F0     /* terminal: echo off (0) / on (1)            */
#define TIOCSRAW   0x54F1     /* terminal: raw (1) / line-buffered (0) mode */
#define TIOCGWINSZ 0x5413     /* terminal: get size (struct winsize *)      */
#define TIOCGPGRP  0x540F     /* terminal: get the foreground process group (int *) */
#define TIOCSPGRP  0x5410     /* terminal: set it (const int *); 0 = no foreground job */

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#endif /* _SYS_IOCTL_H */