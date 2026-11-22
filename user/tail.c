#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

#define MAXN 100
#define LW   200

static char ring[MAXN][LW];

static void tail_fd(int fd, unsigned int n) {
    char buf[512], line[LW];
    unsigned int len = 0, total = 0;
    int r;

    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < r; i++) {
            if (buf[i] == '\n') {
                line[len] = '\0';
                for (unsigned int k = 0; k <= len; k++) ring[total % n][k] = line[k];
                total++;
                len = 0;
            } else if (len < LW - 1) {
                line[len++] = buf[i];
            }
        }
    }
    if (len > 0) {                                  // last line without a newline
        line[len] = '\0';
        for (unsigned int k = 0; k <= len; k++) ring[total % n][k] = line[k];
        total++;
    }

    unsigned int start = total > n ? total - n : 0;
    for (unsigned int k = start; k < total; k++) {
        puts(ring[k % n]);
        puts("\n");
    }
}

int main(int argc, char **argv) {
    unsigned int n = 10;
    int i = 1, status = 0;

    if (i < argc && argv[i][0] == '-' && argv[i][1]) {      // -n N, -nN or -N
        const char *s = argv[i] + 1;
        if (*s == 'n') {
            s++;
            if (!*s) {
                if (++i >= argc) { puts("usage: tail [-n N] [FILE]\n"); return 1; }
                s = argv[i];
            }
        }
        n = 0;
        while (*s >= '0' && *s <= '9') n = n * 10 + (unsigned int)(*s++ - '0');
        if (*s) { puts("tail: invalid line count\n"); return 1; }
        i++;
    }
    if (n < 1) n = 1;
    if (n > MAXN) n = MAXN;

    if (i >= argc) {
        tail_fd(0, n);
        return 0;
    }

    int fd = open(argv[i], O_RDONLY);
    if (fd < 0) { eprint("tail", argv[i], fd); return 1; }
    tail_fd(fd, n);
    close(fd);
    return status;
}