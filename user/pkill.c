#include <unistd.h>
#include "ulib.h"
#include "sigutil.h"

int main(int argc, char **argv) {
    int sig = SIGTERM, i = 1;

    if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        sig = parse_sig_arg(argv[i]);
        if (sig < 0) {
            puts("pkill: unknown signal\n");
            return 1;
        }
        i++;
    }
    if (i >= argc) {
        puts("usage: pkill [-SIGNAL] NAME\n");
        return 1;
    }

    const char *name = argv[i];
    int self = getpid(), killed = 0;
    struct procinfo pi;

    for (int idx = 0; ; idx++) {
        int r = procinfo(idx, &pi);
        if (r < 0) break;                           // past the last slot
        if (r == 0) continue;                       // unused slot
        if (pi.pid == self || pi.pid == 1 || pi.state == 'Z') continue;
        if (!streq(pi.name, name)) continue;

        if (kill(pi.pid, sig) == 0) killed++;
    }
    return killed ? 0 : 1;
}