#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdint.h>

#define AHCI_MAX_DRIVES 32

struct ahci_drive {
    int      present;
    uint32_t sectors;       // total 512-byte sectors (clamped to 2^32 - 1)
    char     model[41];
};

int  ahci_init(void);                                   // returns number of drives found
const struct ahci_drive *ahci_get(int idx);             // NULL if no such drive
int  ahci_read (int idx, uint32_t lba, uint32_t count, void *buf);         // 0 = ok
int  ahci_write(int idx, uint32_t lba, uint32_t count, const void *buf);  // 0 = ok

#endif