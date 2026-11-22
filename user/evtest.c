#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "ulib.h"

int main(void) {
    struct pollfd p[2];
    unsigned char m[3], k;

    int mouse = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    if (mouse < 0) {
        eprint("evtest", "/dev/input/mice", mouse);
        return 1;
    }

    ioctl(0, TIOCSRAW, 1);
    puts("move the mouse or press keys ('q' quits)\n");

    for (;;) {
        p[0].fd = 0;     p[0].events = POLLIN;
        p[1].fd = mouse; p[1].events = POLLIN;

        int n = poll(p, 2, 2000);
        if (n < 0) break;
        if (n == 0) { puts("(nothing for 2 seconds)\n"); continue; }

        if (p[1].revents & POLLIN) {
            while (read(mouse, m, 3) == 3) {                // non-blocking: stops when empty
                int dx = m[1] - ((m[0] << 4) & 0x100);
                int dy = m[2] - ((m[0] << 3) & 0x100);
                puts("mouse dx="); print_int(dx);
                puts(" dy=");      print_int(dy);
                puts(" buttons="); print_int(m[0] & 7);
                puts("\n");
            }
        }

        if (p[0].revents & POLLIN) {
            if (read(0, &k, 1) == 1) {
                if (k == 'q') break;
                puts("key ");
                print_int(k);
                puts("\n");
            }
        }
    }

    ioctl(0, TIOCSRAW, 0);
    close(mouse);
    return 0;
}