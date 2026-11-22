#include <unistd.h>
#include "ulib.h"

int main(int argc, char **argv) {
    struct utsname u;
    int s = 0, n = 0, r = 0, v = 0, m = 0;

    if (uname(&u) < 0) {
        puts("uname: failed\n");
        return 1;
    }

    if (argc == 1) s = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') { puts("usage: uname [-asnrvm]\n"); return 1; }
        for (const char *f = argv[i] + 1; *f; f++) {
            switch (*f) {
            case 'a': s = n = r = v = m = 1; break;
            case 's': s = 1; break;
            case 'n': n = 1; break;
            case 'r': r = 1; break;
            case 'v': v = 1; break;
            case 'm': m = 1; break;
            default:  puts("usage: uname [-asnrvm]\n"); return 1;
            }
        }
    }

    int first = 1;
    if (s) { if (!first) puts(" "); puts(u.sysname);  first = 0; }
    if (n) { if (!first) puts(" "); puts(u.nodename); first = 0; }
    if (r) { if (!first) puts(" "); puts(u.release);  first = 0; }
    if (v) { if (!first) puts(" "); puts(u.version);  first = 0; }
    if (m) { if (!first) puts(" "); puts(u.machine);  first = 0; }
    puts("\n");
    return 0;
}