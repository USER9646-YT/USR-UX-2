#include <unistd.h>
#include "ulib.h"
#include "procread.h"

static void two(unsigned int v) {
    if (v < 10) puts("0");
    print_int((int)v);
}

int main(void) {
    char up[64], st[512];

    int r = proc_slurp("/proc/uptime", up, sizeof(up));
    if (r < 0) { eprint("uptime", "/proc/uptime", r); return 1; }

    unsigned int secs = 0;
    for (const char *p = up; *p >= '0' && *p <= '9'; p++) secs = secs * 10 + (unsigned int)(*p - '0');

    unsigned int days = secs / 86400, h = (secs % 86400) / 3600, m = (secs % 3600) / 60, s = secs % 60;

    puts("up ");
    if (days) {
        print_int((int)days);
        puts(days == 1 ? " day, " : " days, ");
    }
    print_int((int)h); puts(":"); two(m); puts(":"); two(s);

    if (proc_slurp("/proc/stat", st, sizeof(st)) >= 0) {
        int found;
        unsigned int total = proc_number(st, "procs_total", &found);
        if (found) {
            puts(", ");
            print_int((int)total);
            puts(total == 1 ? " process" : " processes");
        }
    }
    puts("\n");
    return 0;
}