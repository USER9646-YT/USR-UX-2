#include <unistd.h>
#include "ulib.h"

int main(void) {
    int r = reboot(RB_HALT);            // only returns if it failed
    eprint("halt", "cannot halt", r);
    return 1;
}