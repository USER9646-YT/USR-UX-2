#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mntent.h>

#define WEAK __attribute__((weak))

WEAK FILE *setmntent(const char *file, const char *mode) {
    return fopen(file, mode);
}

WEAK int endmntent(FILE *f) {
    if (f) fclose(f);
    return 1;
}

WEAK struct mntent *getmntent_r(FILE *f, struct mntent *me, char *b, int size) {
    while (fgets(b, size, f)) {
        char *fld[6] = { 0, 0, 0, 0, 0, 0 };
        char *p = b;
        int n = 0;
        size_t len = strlen(b);

        while (len && (b[len - 1] == '\n' || b[len - 1] == '\r')) b[--len] = '\0';
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;

        while (*p && n < 6) {
            fld[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (!*p) break;
            *p++ = '\0';
            while (*p == ' ' || *p == '\t') p++;
        }
        if (n < 4) continue;

        me->mnt_fsname = fld[0];
        me->mnt_dir    = fld[1];
        me->mnt_type   = fld[2];
        me->mnt_opts   = fld[3];
        me->mnt_freq   = n > 4 ? atoi(fld[4]) : 0;
        me->mnt_passno = n > 5 ? atoi(fld[5]) : 0;
        return me;
    }
    return NULL;
}

WEAK struct mntent *getmntent(FILE *f) {
    static struct mntent me;
    static char buf[1024];
    return getmntent_r(f, &me, buf, sizeof(buf));
}

WEAK int addmntent(FILE *f, const struct mntent *me) {
    if (fprintf(f, "%s %s %s %s %d %d\n", me->mnt_fsname, me->mnt_dir, me->mnt_type,
                me->mnt_opts, me->mnt_freq, me->mnt_passno) < 0)
        return 1;
    return 0;
}

WEAK char *hasmntopt(const struct mntent *me, const char *opt) {
    size_t len = strlen(opt);
    char *p = me->mnt_opts;

    while (p && *p) {
        if (strncmp(p, opt, len) == 0 && (p[len] == '\0' || p[len] == ',' || p[len] == '='))
            return p;
        p = strchr(p, ',');
        if (p) p++;
    }
    return NULL;
}