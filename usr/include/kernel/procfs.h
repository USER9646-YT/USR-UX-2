#ifndef KERNEL_PROCFS_H
#define KERNEL_PROCFS_H

#include "vfs.h"

// Builds the /proc filesystem. Returns its root directory (one reference held),
// meant for vfs_mount("/proc", ...). Everything in it is generated when read.
struct vfs_node *procfs_create(void);

#endif