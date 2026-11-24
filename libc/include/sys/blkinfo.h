#ifndef _SYS_BLKINFO_H
#define _SYS_BLKINFO_H

#include <stdint.h>

#define BLKGETSIZE 0x1260
#define BLKGETINFO 0x12F0

struct blkinfo {
    uint32_t sectors;       // size in 512-byte sectors
    uint32_t start;         // first sector on the disk (0 for a whole disk)
    int32_t  disk;          // disk number
    int32_t  part;          // partition number, 0 for the whole disk
    uint32_t flags;         // 1 = removable
    char     bus[8];        // "ide", "sata", "usb"
    char     model[41];
};

#endif