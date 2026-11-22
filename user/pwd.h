#ifndef PWD_H
#define PWD_H

#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

struct passwd {
    char         name[32];
    unsigned int uid;
    unsigned int gid;
    char         home[64];
    char         shell[64];
};

// Reads a whole text file into buf (NUL-terminated). Returns length or -errno.
static inline int read_whole(const char *path, char *buf, int max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return fd;

    int total = 0, n;
    while (total < max - 1 &&
           (n = read(fd, buf + total, (unsigned int)(max - 1 - total))) > 0)
        total += n;

    close(fd);
    buf[total] = '\0';
    return total;
}

// Splits "a:b:c" in place: returns the next field and advances *p
static inline char *next_field(char **p) {
    char *s = *p;
    if (!s) return 0;

    char *c = s;
    while (*c && *c != ':') c++;
    if (*c) { *c = '\0'; *p = c + 1; }
    else    { *p = 0; }
    return s;
}

static inline unsigned int parse_uint(const char *s) {
    unsigned int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned int)(*s++ - '0');
    return v;
}

static inline void copy_str(char *dst, const char *src, unsigned int max) {
    unsigned int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Finds a user by name (name != 0) or by uid. Returns 0 on success.
static inline int pw_lookup(const char *name, unsigned int uid, struct passwd *out) {
    static char buf[2048];
    if (read_whole("/etc/passwd", buf, sizeof(buf)) < 0) return -1;

    char *line = buf;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int last = (*eol == '\0');
        *eol = '\0';

        char *p       = line;
        char *f_name  = next_field(&p);
        char *f_pass  = next_field(&p);
        char *f_uid   = next_field(&p);
        char *f_gid   = next_field(&p);
        char *f_gecos = next_field(&p);
        char *f_home  = next_field(&p);
        char *f_shell = next_field(&p);
        (void)f_pass;
        (void)f_gecos;

        if (f_name && *f_name && f_uid && f_gid && f_home && f_shell) {
            unsigned int u = parse_uint(f_uid);
            if (name ? streq(f_name, name) : (u == uid)) {
                copy_str(out->name, f_name, sizeof(out->name));
                out->uid = u;
                out->gid = parse_uint(f_gid);
                copy_str(out->home, f_home, sizeof(out->home));
                copy_str(out->shell, f_shell, sizeof(out->shell));
                return 0;
            }
        }

        if (last) break;
        line = eol + 1;
    }
    return -1;
}

#endif