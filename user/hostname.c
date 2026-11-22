#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

#define SYS_sethostname 74

static int set_name(const char *name) {
    int r = __syscall3(SYS_sethostname, (int)name, (int)ustrlen(name), 0);
    if (r < 0) eprint("hostname", name, r);
    return r < 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        struct utsname u;
        uname(&u);
        puts(u.nodename);
        puts("\n");
        return 0;
    }

    if (argc == 3 && streq(argv[1], "-F")) {            // first word of the file
        char buf[80];
        int fd = open(argv[2], O_RDONLY);
        if (fd < 0) { eprint("hostname", argv[2], fd); return 1; }

        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) { puts("hostname: empty file\n"); return 1; }
        buf[n] = '\0';

        char *s = buf;
        while (*s == ' ' || *s == '\n' || *s == '\t') s++;
        char *e = s;
        while (*e && *e != ' ' && *e != '\n' && *e != '\t' && *e != '\r') e++;
        *e = '\0';
        if (!*s) { puts("hostname: empty file\n"); return 1; }
        return set_name(s);
    }

    if (argc == 2 && argv[1][0] != '-')
        return set_name(argv[1]);

    puts("usage: hostname [NAME | -F FILE]\n");
    return 1;
}