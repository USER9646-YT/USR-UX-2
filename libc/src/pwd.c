#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct passwd pw;
static char line[256];

static struct passwd *parse(const char *l) {
    char *f[7];
    int n = 0;

    strncpy(line, l, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    char *p = line;
    f[n++] = p;
    while (*p && n < 7) {
        if (*p == ':') { *p = '\0'; f[n++] = p + 1; }
        p++;
    }
    if (n < 7) return NULL;

    char *nl = strchr(f[6], '\n');
    if (nl) *nl = '\0';

    pw.pw_name   = f[0];
    pw.pw_passwd = f[1];
    pw.pw_uid    = (uid_t)strtoul(f[2], NULL, 10);
    pw.pw_gid    = (gid_t)strtoul(f[3], NULL, 10);
    pw.pw_gecos  = f[4];
    pw.pw_dir    = f[5];
    pw.pw_shell  = f[6];
    return &pw;
}

static struct passwd *lookup(const char *name, uid_t uid, int by_name) {
    char buf[256];
    FILE *f = fopen("/etc/passwd", "r");

    if (!f) return NULL;
    while (fgets(buf, sizeof(buf), f)) {
        struct passwd *p = parse(buf);
        if (!p) continue;
        if (by_name ? strcmp(p->pw_name, name) == 0 : p->pw_uid == uid) {
            fclose(f);
            return p;
        }
    }
    fclose(f);
    return NULL;
}

struct passwd *getpwnam(const char *name) { return lookup(name, 0, 1); }
struct passwd *getpwuid(uid_t uid)        { return lookup(NULL, uid, 0); }

char *getlogin(void) {
    char *n = getenv("LOGNAME");
    if (n) return n;

    struct passwd *p = getpwuid(getuid());
    return p ? p->pw_name : NULL;
}