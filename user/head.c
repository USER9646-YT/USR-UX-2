#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

static void head_fd(int fd, int n) {
    char buf[512];
    int r, lines = 0;

    while (lines < n && (r = read(fd, buf, sizeof(buf))) > 0) {
        int out = 0;
        for (int i = 0; i < r; i++) {
            out = i + 1;
            if (buf[i] == '\n' && ++lines >= n) break;
        }
        write(1, buf, (unsigned int)out);
    }
}

int main(int argc, char **argv) {
    int n = 10, i = 1, status = 0;

    if (i < argc && argv[i][0] == '-' && argv[i][1]) {      // -n N, -nN or -N
        const char *s = argv[i] + 1;
        if (*s == 'n') {
            s++;
            if (!*s) {
                if (++i >= argc) { puts("usage: head [-n N] [FILE...]\n"); return 1; }
                s = argv[i];
            }
        }
        n = 0;
        while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
        if (*s) { puts("head: invalid line count\n"); return 1; }
        i++;
    }

    if (i >= argc) {
        head_fd(0, n);
        return 0;
    }

    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { eprint("head", argv[i], fd); status = 1; continue; }
        head_fd(fd, n);
        close(fd);
    }
    return status;
}