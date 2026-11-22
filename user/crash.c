#include <unistd.h>
#include "ulib.h"

int main(void) {
    puts("about to crash...\n");
    *(volatile int *)0 = 1;
    return 0;
}