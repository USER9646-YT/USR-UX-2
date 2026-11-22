#include <unistd.h>
#include "ulib.h"

int main(int argc, char **argv) {
    int status = 0;

    if (argc < 2) {
        puts("usage: rmdir DIR...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int r = rmdir(argv[i]);
        if (r < 0) { eprint("rmdir", argv[i], r); status = 1; }
    }
    return status;
}