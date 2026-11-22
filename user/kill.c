#include <unistd.h>
#include "ulib.h"
#include "sigutil.h"

int main(int argc, char **argv) {
    int sig = SIGTERM, i = 1, status = 0;

    if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        sig = parse_sig_arg(argv[i]);
        if (sig < 0) {
            puts("kill: unknown signal\n");
            return 1;
        }
        i++;
    }
    if (i >= argc) {
        puts("usage: kill [-SIGNAL] PID...\n");
        return 1;
    }

    for (; i < argc; i++) {
        int pid = 0;
        const char *s = argv[i];

        while (*s >= '0' && *s <= '9') pid = pid * 10 + (*s++ - '0');
        if (*s || pid <= 0) {
            puts("kill: invalid pid: ");
            puts(argv[i]);
            puts("\n");
            status = 1;
            continue;
        }

        int r = kill(pid, sig);
        if (r < 0) {
            eprint("kill", argv[i], r);
            status = 1;
        }
    }
    return status;
}