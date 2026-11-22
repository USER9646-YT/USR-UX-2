#include <unistd.h>
#include "ulib.h"

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;

    for (; envp && *envp; envp++) {
        puts(*envp);
        puts("\n");
    }
    return 0;
}