#ifndef KERNEL_PART_H
#define KERNEL_PART_H

#include <stdint.h>

// Looks for the first Linux (type 0x83) partition in the MBR of an ATA drive.
// Returns 0 and fills in its first sector and size, or -1 if there is none.
int part_find_linux(int drive, uint32_t *start_lba, uint32_t *sectors);

#endif