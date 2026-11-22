#ifndef _TERMIOS_H
#define _TERMIOS_H

#define NCCS 19

struct termios {
    unsigned int  c_iflag;
    unsigned int  c_oflag;
    unsigned int  c_cflag;
    unsigned int  c_lflag;
    unsigned char c_line;
    unsigned char c_cc[NCCS];
};

// c_cc indexes
#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VTIME   5
#define VMIN    6
#define VSTART  8
#define VSTOP   9
#define VSUSP  10

// c_iflag
#define ICRNL  0000400
#define IXON   0002000

// c_oflag
#define OPOST  0000001
#define ONLCR  0000004

// c_cflag
#define CS8    0000060
#define CREAD  0000200
#define HUPCL  0002000
#define B38400 0000017

// c_lflag
#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010
#define ECHOE  0000020
#define ECHOK  0000040
#define IEXTEN 0100000

#define TCGETS  0x5401
#define TCSETS  0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#ifndef __KERNEL__
#include <unistd.h>

static inline int tcgetattr(int fd, struct termios *t) {
    return ioctl(fd, TCGETS, (unsigned int)t);
}
static inline int tcsetattr(int fd, int action, const struct termios *t) {
    return ioctl(fd, TCSETS + action, (unsigned int)t);
}
static inline int isatty(int fd) {
    struct termios t;
    return tcgetattr(fd, &t) == 0;
}
#endif

#endif