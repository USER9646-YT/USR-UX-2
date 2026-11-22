#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFLNK  0120000

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)

struct stat {
    unsigned int st_ino;
    unsigned int st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int st_size;
};

#endif /* _SYS_STAT_H */