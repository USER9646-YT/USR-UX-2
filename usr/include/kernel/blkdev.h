#ifndef KERNEL_BLKDEV_H
#define KERNEL_BLKDEV_H

#include <stdint.h>
#include "vfs.h"

// Adds /dev/diskX (the whole disk) and /dev/diskXpY (partition Y) for every disk to the devfs root
void blkdev_register(struct vfs_node *devroot);

// If n is a block device node: 0 and its disk number, first sector and size; else -1
int  blkdev_from_node(struct vfs_node *n, int *disk, uint32_t *start, uint32_t *sectors);

// The name ("disk1p1") of the device that starts at `start` on `disk`. 0 = found
int  blkdev_name(int disk, uint32_t start, char *out, uint32_t size);

#endif