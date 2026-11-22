#include <unistd.h>
#include "ulib.h"
#include "sha256.h"

int main(int argc, char **argv) {
    char out[65];

    if (argc != 2) {
        puts("usage: sha256 TEXT\n");
        return 1;
    }
    sha256_hex(argv[1], ustrlen(argv[1]), out);
    puts(out);
    puts("\n");
    return 0;
}