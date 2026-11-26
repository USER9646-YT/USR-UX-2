#ifndef _MNTENT_H
#define _MNTENT_H
#include <stdio.h>

#define MNTTAB  "/etc/fstab"
#define MOUNTED "/proc/mounts"

struct mntent {
    char *mnt_fsname;
    char *mnt_dir;
    char *mnt_type;
    char *mnt_opts;
    int   mnt_freq;
    int   mnt_passno;
};

FILE          *setmntent(const char *, const char *);
struct mntent *getmntent(FILE *);
struct mntent *getmntent_r(FILE *, struct mntent *, char *, int);
int            addmntent(FILE *, const struct mntent *);
int            endmntent(FILE *);
char          *hasmntopt(const struct mntent *, const char *);

#endif