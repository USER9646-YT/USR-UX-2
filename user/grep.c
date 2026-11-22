#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

static int opt_i, opt_n, opt_v;

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int contains(const char *line, const char *pat) {
    if (!*pat) return 1;

    for (const char *s = line; *s; s++) {
        const char *a = s, *b = pat;
        while (*a && *b && (opt_i ? lower(*a) == lower(*b) : *a == *b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

// Prints the line if it matches; returns 1 when it printed something
static int check_line(const char *line, int lineno, const char *name,
                      int show_name, const char *pat) {
    int hit = contains(line, pat);
    if (opt_v) hit = !hit;
    if (!hit) return 0;

    if (show_name) { puts(name); puts(":"); }
    if (opt_n)     { print_int(lineno); puts(":"); }
    puts(line);
    puts("\n");
    return 1;
}

static int grep_fd(int fd, const char *name, const char *pat, int show_name) {
    char buf[512], line[1024];
    unsigned int len = 0;
    int n, lineno = 0, matches = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[len] = '\0';
                matches += check_line(line, ++lineno, name, show_name, pat);
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = buf[i];
            }
        }
    }
    if (len > 0) {                                // last line without a newline
        line[len] = '\0';
        matches += check_line(line, ++lineno, name, show_name, pat);
    }
    return matches;
}

int main(int argc, char **argv) {
    int i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'i')      opt_i = 1;
            else if (*f == 'n') opt_n = 1;
            else if (*f == 'v') opt_v = 1;
            else { puts("usage: grep [-inv] PATTERN [FILE...]\n"); return 2; }
        }
    }
    if (i >= argc) {
        puts("usage: grep [-inv] PATTERN [FILE...]\n");
        return 2;
    }

    const char *pat = argv[i++];
    int matches = 0, status = 0;

    if (i >= argc)
        return grep_fd(0, "(stdin)", pat, 0) > 0 ? 0 : 1;

    int show_name = (argc - i) > 1;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) { eprint("grep", argv[i], fd); status = 2; continue; }
        matches += grep_fd(fd, argv[i], pat, show_name);
        close(fd);
    }
    return status ? status : (matches > 0 ? 0 : 1);
}