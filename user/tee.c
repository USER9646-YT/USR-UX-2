#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

int main(int argc, char **argv) {
    int append = 0, i = 1, nfd = 0, status = 0;
    int fds[8];
    char buf[512];
    int n;

    if (i < argc && streq(argv[i], "-a")) { append = 1; i++; }

    for (; i < argc && nfd < 8; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
        if (fd < 0) { eprint("tee", argv[i], fd); status = 1; continue; }
        fds[nfd++] = fd;
    }

    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, (unsigned int)n);
        for (int k = 0; k < nfd; k++) write(fds[k], buf, (unsigned int)n);
    }

    for (int k = 0; k < nfd; k++) close(fds[k]);
    return status;
}