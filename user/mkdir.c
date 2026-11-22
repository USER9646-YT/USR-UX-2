#include <unistd.h>
#include "ulib.h"
#include "fsutil.h"

static int make_dir(const char *path, int parents) {
    int r;

    if (parents) {                                // mkdir -p: create every missing parent
        char buf[PATH_BUF];
        unsigned int len = 0;

        while (path[len] && len < sizeof(buf) - 1) { buf[len] = path[len]; len++; }
        buf[len] = '\0';

        for (unsigned int k = 1; k < len; k++) {
            if (buf[k] == '/') {
                buf[k] = '\0';
                mkdir(buf, 0755);                 // "already exists" is fine here
                buf[k] = '/';
            }
        }
        r = mkdir(buf, 0755);
        if (r < 0 && r != -17) { eprint("mkdir", path, r); return -1; }
        return 0;
    }

    r = mkdir(path, 0755);
    if (r < 0) { eprint("mkdir", path, r); return -1; }
    return 0;
}

int main(int argc, char **argv) {
    int parents = 0, i = 1, status = 0;

    if (i < argc && streq(argv[i], "-p")) { parents = 1; i++; }
    if (i >= argc) {
        puts("usage: mkdir [-p] DIR...\n");
        return 1;
    }

    for (; i < argc; i++)
        if (make_dir(argv[i], parents) < 0) status = 1;
    return status;
}