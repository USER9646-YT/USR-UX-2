#ifndef PROCREAD_H
#define PROCREAD_H

#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

// Reads a small text file (a /proc file) into buf, NUL-terminated. Returns its length or -errno.
static inline int proc_slurp(const char *path, char *buf, int max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return fd;

    int total = 0, n;
    while (total < max - 1 && (n = read(fd, buf + total, (unsigned int)(max - 1 - total))) > 0)
        total += n;
    close(fd);
    buf[total] = '\0';
    return total;
}

// Finds "key" at the start of a line and returns the unsigned number after it
// (spaces and ':' skipped). Returns 0 and sets *found = 0 if the key is missing.
static inline unsigned int proc_number(const char *buf, const char *key, int *found) {
    unsigned int klen = ustrlen(key);

    for (const char *line = buf; *line; ) {
        unsigned int i = 0;
        while (i < klen && line[i] == key[i]) i++;

        if (i == klen) {
            const char *p = line + klen;
            while (*p == ':' || *p == ' ' || *p == '\t') p++;

            unsigned int v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned int)(*p++ - '0');
            if (found) *found = 1;
            return v;
        }
        while (*line && *line != '\n') line++;       // next line
        if (*line) line++;
    }
    if (found) *found = 0;
    return 0;
}

#endif