#include <unistd.h>
#include "ulib.h"

int main(void) {
    int r = reboot(RB_AUTOBOOT);
    eprint("reboot", "cannot reboot", r);
    return 1;
}