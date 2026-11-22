#ifndef KERNEL_EXT2_H
#define KERNEL_EXT2_H

#include <stdint.h>
#include "vfs.h"

// Mounts the ext2 filesystem found on ATA drive `drive`, starting at sector
//   `start_lba` (0 for a whole-disk image). This returns the root directory node
//   (one reference held, meant to be given to vfs_mount) or NULL on failure.
struct vfs_node *ext2_mount(int drive, uint32_t start_lba);

#endif