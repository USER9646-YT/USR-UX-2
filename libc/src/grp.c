#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <grp.h>

#define WEAK __attribute__((weak))
#define MAXMEM 64

static FILE        *grf;
static char         line[1024];
static char        *members[MAXMEM + 1];
static struct group grp;

// "name:passwd:gid:member,member" -> grp. 0 = ok, -1 = comment or malformed
static int parse_line(char *s) {
    char *f[4];
    size_t n = strlen(s);

    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
    if (!*s || *s == '#') return -1;

    for (int i = 0; i < 4; i++) {
        f[i] = s;
        if (i < 3) {
            s = strchr(s, ':');
            if (!s) return -1;
            *s++ = '\0';
        }
    }

    int k = 0;
    char *m = f[3];
    while (*m && k < MAXMEM) {
        members[k++] = m;
        m = strchr(m, ',');
        if (!m) break;
        *m++ = '\0';
    }
    members[k] = NULL;

    grp.gr_name   = f[0];
    grp.gr_passwd = f[1];
    grp.gr_gid    = (gid_t)strtoul(f[2], NULL, 10);
    grp.gr_mem    = members;
    return 0;
}

WEAK void setgrent(void) {
    if (grf) rewind(grf);
}

WEAK void endgrent(void) {
    if (grf) fclose(grf);
    grf = NULL;
}

WEAK struct group *getgrent(void) {
    if (!grf) grf = fopen("/etc/group", "r");
    if (!grf) return NULL;
    while (fgets(line, sizeof(line), grf))
        if (parse_line(line) == 0) return &grp;
    return NULL;
}

WEAK struct group *getgrnam(const char *name) {
    struct group *g;

    endgrent();
    while ((g = getgrent()) != NULL) {
        if (strcmp(g->gr_name, name) == 0) { endgrent(); return g; }
    }
    endgrent();
    return NULL;
}

WEAK struct group *getgrgid(gid_t gid) {
    struct group *g;

    endgrent();
    while ((g = getgrent()) != NULL) {
        if (g->gr_gid == gid) { endgrent(); return g; }
    }
    endgrent();
    return NULL;
}

WEAK int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups) {
    static char lb[1024];
    gid_t list[32];
    int cnt = 0;

    list[cnt++] = group;
    FILE *f = fopen("/etc/group", "r");
    if (f) {
        while (cnt < 32 && fgets(lb, sizeof(lb), f)) {
            if (parse_line(lb) != 0 || grp.gr_gid == group) continue;
            for (char **m = grp.gr_mem; *m; m++)
                if (strcmp(*m, user) == 0) { list[cnt++] = grp.gr_gid; break; }
        }
        fclose(f);
    }

    int room = *ngroups;
    for (int i = 0; i < cnt && i < room; i++) groups[i] = list[i];
    *ngroups = cnt;
    return cnt <= room ? cnt : -1;
}

WEAK int initgroups(const char *user, gid_t group) {
    (void)user;
    (void)group;
    return 0;                       // one group per process here
}