#include <unistd.h>

static const char msg[] = "Hello from a real ELF program, loaded from ext2!\n";
static volatile int data_val = 1234;      /* .data */
static volatile int bss_val;              /* .bss, must start as 0 */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write(1, msg, sizeof(msg) - 1);

    data_val += 1;                        /* writing to .data must work */
    if (data_val != 1235) return 1;
    if (bss_val != 0)     return 2;
    return 0;
}