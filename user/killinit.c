#include <unistd.h>
#include <signal.h>
#include "ulib.h"

int main(void) {
    puts("killinit: this sends SIGKILL to init (pid 1), which panics the kernel.\n");

    for (int i = 3; i > 0; i--) {
        print_int(i);
        puts("... (Ctrl-C to abort)\n");
        sleep(1);
    }

    int r = kill(1, SIGKILL);
    if (r < 0) {
        eprint("killinit", "pid 1", r);      // only root may do this
        return 1;
    }

    puts("signal sent, waiting for the kernel panic\n");
    for (;;) sleep(1);
}