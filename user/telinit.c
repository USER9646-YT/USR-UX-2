#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

int main(int argc, char **argv) {
    if (argc != 2 || argv[1][1] ||
        !((argv[1][0] >= '0' && argv[1][0] <= '6') || argv[1][0] == 'S' || argv[1][0] == 's')) {
        puts("usage: telinit 0|1|2|3|4|5|6|S\n");
        return 1;
    }

    // /run is writable by root only, so only root can talk to init
    int fd = open("/run/initctl", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        eprint("telinit", "/run/initctl", fd);
        return 1;
    }

    char msg[2] = { argv[1][0], '\n' };
    write(fd, msg, 2);
    close(fd);
    return 0;
}