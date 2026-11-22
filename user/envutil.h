#ifndef ENVUTIL_H
#define ENVUTIL_H

#include "ulib.h"
#include "pwd.h"

// Builds HOME, USER, LOGNAME, SHELL, PATH and TERM for a user.
// store needs 512 bytes, envp room for 7 pointers.
static inline void make_env(const struct passwd *pw, char *store, unsigned int size, char **envp) {
    const char *keys[6] = { "HOME=", "USER=", "LOGNAME=", "SHELL=", "PATH=", "TERM=" };
    const char *vals[6] = { pw->home, pw->name, pw->name, pw->shell, "/bin:/sbin", "linux" };
    unsigned int pos = 0;
    int n = 0;

    for (int i = 0; i < 6; i++) {
        unsigned int start = pos;

        for (const char *k = keys[i]; *k && pos < size - 1; k++) store[pos++] = *k;
        for (const char *v = vals[i]; *v && pos < size - 1; v++) store[pos++] = *v;
        store[pos++] = '\0';
        envp[n++] = store + start;
    }
    envp[n] = 0;
}

#endif