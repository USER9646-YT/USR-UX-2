#ifndef AUTH_H
#define AUTH_H

#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"
#include "sha256.h"
#include "pwd.h"

static inline void wipe(char *p, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) p[i] = 0;
}

// /etc/shadow lines look like  name:salt$sha256hex  where hex = SHA256(salt + password).
// Needs the privilege to read /etc/shadow.
static inline int check_password(const char *user, const char *pass) {
    static char buf[2048];
    if (read_whole("/etc/shadow", buf, sizeof(buf)) < 0) return 0;

    char *line = buf;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int last = (*eol == '\0');
        *eol = '\0';

        char *p      = line;
        char *f_name = next_field(&p);
        char *f_hash = next_field(&p);

        if (f_name && f_hash && streq(f_name, user)) {
            char *dollar = f_hash;
            while (*dollar && *dollar != '$') dollar++;
            if (!*dollar) return 0;                 // locked account
            *dollar = '\0';

            const char *salt = f_hash;
            const char *hex  = dollar + 1;

            char tmp[200];
            unsigned int n = 0;
            for (const char *c = salt; *c && n < 40; c++) tmp[n++] = *c;
            for (const char *c = pass; *c && n < sizeof(tmp) - 1; c++) tmp[n++] = *c;
            tmp[n] = '\0';

            char digest[65];
            sha256_hex(tmp, n, digest);
            wipe(tmp, sizeof(tmp));
            return streq(digest, hex);
        }

        if (last) break;
        line = eol + 1;
    }
    return 0;
}

// Prints a prompt and reads a line without echoing it. Returns its length, or -1.
static inline int read_password(const char *prompt, char *buf, int max) {
    puts(prompt);
    ioctl(0, TIOCSECHO, 0);
    int n = read(0, buf, (unsigned int)(max - 1));
    ioctl(0, TIOCSECHO, 1);

    if (n <= 0) return -1;
    if (buf[n - 1] == '\n') n--;
    buf[n] = '\0';
    return n;
}

#endif