#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "internal.h"

int errno;
char **environ;

extern int main(int, char **, char **);

#define MAX_ATEXIT 32
static void (*atexit_fns[MAX_ATEXIT])(void);
static int natexit;

int atexit(void (*fn)(void)) {
    if (natexit >= MAX_ATEXIT) return -1;
    atexit_fns[natexit++] = fn;
    return 0;
}

void exit(int status) {
    while (natexit > 0) atexit_fns[--natexit]();
    __stdio_flush_all();
    _exit(status);
}

void _Exit(int status) {
    _exit(status);
}

void abort(void) {
    __stdio_flush_all();
    kill(getpid(), SIGABRT);
    _exit(134);
}

void __assert_fail(const char *expr, const char *file, int line, const char *func);

void __libc_start_main(int argc, char **argv, char **envp) {
    environ = envp;
    exit(main(argc, argv, envp));
}