#ifndef _DIRENT_H
#define _DIRENT_H

#define DT_REG 1
#define DT_DIR 2
#define DT_LNK 3

struct dirent {
    unsigned int d_ino;
    unsigned int d_type;
    char         d_name[256];
};

#endif