#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

int main(int argc, char **argv) {
    int status = 0;

    if (argc < 2) {
        puts("usage: touch FILE...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT);
        if (fd < 0) { eprint("touch", argv[i], fd); status = 1; continue; }
        close(fd);
    }
    return status;
}