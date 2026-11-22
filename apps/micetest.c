#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>

static const unsigned char imps2[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };

int main(void) {
    unsigned char buf[4] = { 0, 0, 0, 0 };
    int n, events = 0, empty = 0;

    int fd = open("/dev/input/mice", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("open failed: %d\n", errno);
        return 1;
    }
    printf("fd = %d\n", fd);

    n = write(fd, imps2, sizeof(imps2));
    printf("init write returned %d (driver expects 6)\n", n);
    n = read(fd, buf, 4);
    printf("init read returned %d, first byte 0x%x (driver expects 1 and 0xfa)\n", n, buf[0]);

    printf("move the mouse for 15 seconds...\n");
    for (int i = 0; i < 150; i++) {
        fd_set r;
        struct timeval tv;

        FD_ZERO(&r);
        FD_SET(fd, &r);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int s = select(fd + 1, &r, NULL, NULL, &tv);
        if (s > 0 && FD_ISSET(fd, &r)) {
            n = read(fd, buf, 4);
            if (n > 0) {
                events++;
                printf("read %d bytes: %x %x %x %x\n", n, buf[0], buf[1], buf[2], buf[3]);
            } else {
                empty++;                // select said ready, but there was nothing to read
            }
        }
    }
    printf("packets: %d, ready-but-empty: %d\n", events, empty);
    close(fd);
    return 0;
}