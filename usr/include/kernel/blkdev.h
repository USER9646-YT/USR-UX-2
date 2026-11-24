#ifndef KERNEL_BLKDEV_H
#define KERNEL_BLKDEV_H

#include "vfs.h"

// Adds /dev/diskX (the whole disk) and /dev/diskXpY (partition Y) for every ATA drive
void blkdev_register(struct vfs_node *devroot);

#endif