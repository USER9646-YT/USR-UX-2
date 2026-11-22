#ifndef KERNEL_DEVFS_H
#define KERNEL_DEVFS_H

#include "vfs.h"

// Builds the /dev filesystem (null, zero, random, urandom, console, tty).
// Returns the root directory, meant for vfs_mount("/dev", ...).
struct vfs_node *devfs_create(void);

#endif