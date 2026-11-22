#include <stdio.h>
#include <unistd.h>

// cc: tcc with the flags this OS needs, so "cc hello.c -o hello" just works.
//   -static             there is no dynamic linker
//   -Ttext=0x40000000   programs must load at 0x40000000 and above
int main(int argc, char **argv) {
    char *args[16];                               // the kernel passes at most 16 arguments
    int n = 0;

    if (argc > 13) {
        fprintf(stderr, "cc: too many arguments (at most 12)\n");
        return 1;
    }

    args[n++] = "tcc";
    args[n++] = "-static";
    args[n++] = "-Wl,-Ttext=0x40000000";
    for (int i = 1; i < argc; i++) args[n++] = argv[i];
    args[n] = NULL;

    execvp("tcc", args);
    perror("cc: cannot run tcc");
    return 127;
}