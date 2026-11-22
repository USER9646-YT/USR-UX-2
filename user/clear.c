#include <unistd.h>
#include "ulib.h"

int main(void) {
    puts("\033[0m\033[2J\033[H");
    return 0;
}