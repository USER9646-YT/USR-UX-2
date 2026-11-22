#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

int main(void) {
    unsigned char p[3];

    int fd = open("/dev/input/mice", O_RDONLY);
    if (fd < 0) {
        eprint("mousetest", "/dev/input/mice", fd);
        return 1;
    }

    puts("move the mouse (30 packets)...\n");
    for (int i = 0; i < 30; i++) {
        if (read(fd, p, 3) != 3) break;

        int dx = p[1] - ((p[0] << 4) & 0x100);      // 9-bit signed values
        int dy = p[2] - ((p[0] << 3) & 0x100);

        puts("dx=");   print_int(dx);
        puts(" dy=");  print_int(dy);
        puts(" left="); print_int(p[0] & 1);
        puts(" right="); print_int((p[0] >> 1) & 1);
        puts("\n");
    }
    close(fd);
    return 0;
}