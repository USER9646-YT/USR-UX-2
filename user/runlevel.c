#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

int main(void) {
    char buf[16];
    int fd = open("/run/runlevel", O_RDONLY);

    if (fd < 0) {
        puts("unknown\n");
        return 1;
    }

    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) {
        puts("unknown\n");
        return 1;
    }
    write(1, buf, (unsigned int)n);
    return 0;
}