#include <unistd.h>
#include "ulib.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        puts("usage: chmod OCTAL-MODE FILE\n");
        return 1;
    }

    unsigned int mode = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '7') {
            puts("chmod: invalid mode\n");
            return 1;
        }
        mode = mode * 8 + (unsigned int)(*p - '0');
    }

    int r = chmod(argv[2], mode);
    if (r < 0) {
        eprint("chmod", argv[2], r);
        return 1;
    }
    return 0;
}