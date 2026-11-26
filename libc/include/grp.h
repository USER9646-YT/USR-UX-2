#ifndef _GRP_H
#define _GRP_H
#include <sys/types.h>

struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

struct group *getgrnam(const char *);
struct group *getgrgid(gid_t);
struct group *getgrent(void);
void          setgrent(void);
void          endgrent(void);
int           getgrouplist(const char *, gid_t, gid_t *, int *);
int           initgroups(const char *, gid_t);

#endif