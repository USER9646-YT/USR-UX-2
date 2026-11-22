#include <unistd.h>
#include "ulib.h"
#include "procread.h"

static void column(unsigned int v) {                 // right-aligned in 12 characters
    char digits[12];
    int n = 0;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = n; i < 12; i++) puts(" ");
    while (n) { char c[2] = { digits[--n], 0 }; puts(c); }
}

int main(int argc, char **argv) {
    char buf[512];
    int shift = 0;                                    // 0 = KiB, 10 = MiB

    if (argc > 1) {
        if (streq(argv[1], "-m")) shift = 10;
        else if (!streq(argv[1], "-k")) { puts("usage: free [-k|-m]\n"); return 1; }
    }

    int r = proc_slurp("/proc/meminfo", buf, sizeof(buf));
    if (r < 0) { eprint("free", "/proc/meminfo", r); return 1; }

    unsigned int total = proc_number(buf, "MemTotal", 0) >> shift;
    unsigned int free_ = proc_number(buf, "MemFree", 0) >> shift;
    unsigned int used  = total - free_;

    puts("                 total        used        free\n");
    puts("Mem:    ");
    column(total); column(used); column(free_);
    puts("\n");
    return 0;
}