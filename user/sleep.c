#include <unistd.h>
#include <time.h>
#include "ulib.h"

int main(int argc, char **argv) {
    int sec = 0, frac_ms = 0;

    if (argc != 2) {
        puts("usage: sleep SECONDS\n");
        return 1;
    }

    const char *p = argv[1];
    while (*p >= '0' && *p <= '9') sec = sec * 10 + (*p++ - '0');
    if (*p == '.') {
        int mult = 100;
        p++;
        while (*p >= '0' && *p <= '9' && mult > 0) {
            frac_ms += (*p++ - '0') * mult;
            mult /= 10;
        }
    }
    if (*p) {
        puts("sleep: invalid time interval\n");
        return 1;
    }

    struct timespec ts = { sec, frac_ms * 1000000 };
    return nanosleep(&ts, 0) < 0 ? 1 : 0;
}