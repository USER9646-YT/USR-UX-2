#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

struct counts {
    int lines, words, bytes;
};

static void count_fd(int fd, struct counts *c) {
    char buf[512];
    int n, inword = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            c->bytes++;
            if (ch == '\n') c->lines++;
            if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r') {
                inword = 0;
            } else if (!inword) {
                inword = 1;
                c->words++;
            }
        }
    }
}

static void show(const struct counts *c, int l, int w, int b, const char *name) {
    if (l) { print_int(c->lines); puts(" "); }
    if (w) { print_int(c->words); puts(" "); }
    if (b) { print_int(c->bytes); puts(" "); }
    if (name) puts(name);
    puts("\n");
}

int main(int argc, char **argv) {
    int l = 0, w = 0, b = 0, i = 1, status = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'l')      l = 1;
            else if (*f == 'w') w = 1;
            else if (*f == 'c') b = 1;
            else { puts("usage: wc [-lwc] [FILE...]\n"); return 1; }
        }
    }
    if (!l && !w && !b) l = w = b = 1;

    if (i >= argc) {
        struct counts c = { 0, 0, 0 };
        count_fd(0, &c);
        show(&c, l, w, b, 0);
        return 0;
    }

    for (; i < argc; i++) {
        struct counts c = { 0, 0, 0 };
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { eprint("wc", argv[i], fd); status = 1; continue; }
        count_fd(fd, &c);
        close(fd);
        show(&c, l, w, b, argv[i]);
    }
    return status;
}