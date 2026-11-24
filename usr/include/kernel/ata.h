#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512
#define ATA_MAX_DRIVES  16      // most disks the kernel keeps track of (IDE and SATA together)

struct ata_drive {
    int      present;
    uint32_t sectors;       // total 512-byte sectors (LBA28 for IDE, clamped LBA48 for SATA)
    char     model[41];
};

// The kernel's disk list: present IDE drives first, then SATA drives found through AHCI.
// Disk numbers are positions in that list, so disk 0 is the first disk of either kind.
int  ata_init(void);                                   // returns number of disks found
const struct ata_drive *ata_get(int idx);              // NULL if no such disk
int  ata_read (int idx, uint32_t lba, uint32_t count, void *buf);         // 0 = ok
int  ata_write(int idx, uint32_t lba, uint32_t count, const void *buf);  // 0 = ok
const char *ata_bus_name(int idx);                     // "ide", "sata" or "usb"
int         ata_is_removable(int idx);                 // 1 for USB disks

#endif