#ifndef _DIRENT_H
#define _DIRENT_H

struct dirent {
    unsigned int d_ino;
    unsigned int d_type;
    char         d_name[256];
};

#define DT_UNKNOWN 0
#define DT_REG     1
#define DT_DIR     2
#define DT_LNK     3

typedef struct _DIR DIR;

DIR           *opendir(const char *);
struct dirent *readdir(DIR *);
int            closedir(DIR *);
void           rewinddir(DIR *);
#endif