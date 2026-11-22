#ifndef SIGUTIL_H
#define SIGUTIL_H

#include <signal.h>
#include "ulib.h"

static inline int sig_from_name(const char *s) {
    if (s[0] == 'S' && s[1] == 'I' && s[2] == 'G') s += 3;

    if (streq(s, "HUP"))  return SIGHUP;
    if (streq(s, "INT"))  return SIGINT;
    if (streq(s, "QUIT")) return SIGQUIT;
    if (streq(s, "KILL")) return SIGKILL;
    if (streq(s, "TERM")) return SIGTERM;
    return -1;
}

// Parses "-9", "-KILL" or "-SIGTERM". Returns the signal number, or -1.
static inline int parse_sig_arg(const char *arg) {
    if (arg[0] != '-' || !arg[1]) return -1;

    const char *s = arg + 1;
    if (*s >= '0' && *s <= '9') {
        int v = 0;
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
        return *s ? -1 : v;
    }
    return sig_from_name(s);
}

#endif