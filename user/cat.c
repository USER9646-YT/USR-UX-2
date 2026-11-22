#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

static void copy_fd(int fd) {
    char buf[512];
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, (unsigned int)n);
}

int main(int argc, char **argv) {
    int status = 0;

    if (argc < 2) {
        copy_fd(0);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-")) {
            copy_fd(0);
            continue;
        }

        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            eprint("cat", argv[i], fd);
            status = 1;
            continue;
        }
        copy_fd(fd);
        close(fd);
    }
    return status;
}