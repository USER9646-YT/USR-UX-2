#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512
#define ATA_MAX_DRIVES  4

struct ata_drive {
    int      present;
    uint32_t sectors;       // total 512-byte sectors (LBA28)
    char     model[41];
};

int  ata_init(void);                                   // returns number of drives found
const struct ata_drive *ata_get(int idx);              // NULL if no such drive
int  ata_read (int idx, uint32_t lba, uint32_t count, void *buf);         // 0 = ok
int  ata_write(int idx, uint32_t lba, uint32_t count, const void *buf);  // 0 = ok

#endif