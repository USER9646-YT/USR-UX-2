#include <unistd.h>
#include "ulib.h"

int main(void) {
    int r = reboot(RB_POWEROFF);
    eprint("poweroff", "cannot power off", r);
    return 1;
}