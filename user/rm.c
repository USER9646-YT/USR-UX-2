#include <unistd.h>
#include "ulib.h"
#include "fsutil.h"

int main(int argc, char **argv) {
    int recursive = 0, force = 0, i = 1, status = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'r' || *f == 'R') recursive = 1;
            else if (*f == 'f')         force = 1;
            else { puts("usage: rm [-rf] FILE...\n"); return 1; }
        }
    }
    if (i >= argc) {
        puts("usage: rm [-rf] FILE...\n");
        return 1;
    }

    for (; i < argc; i++) {
        struct stat st;
        int r = stat(argv[i], &st);
        if (r < 0) {
            if (!force) { eprint("rm", argv[i], r); status = 1; }
            continue;
        }
        if (fs_remove_tree("rm", argv[i], recursive, 0) < 0) status = 1;
    }
    return status;
}