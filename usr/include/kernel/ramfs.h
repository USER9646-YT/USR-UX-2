#ifndef KERNEL_RAMFS_H
#define KERNEL_RAMFS_H

#include "vfs.h"

// Creates a new, empty in-memory filesystem and returns its root directory
// (one reference held, meant to be handed to vfs_mount).
struct vfs_node *ramfs_create_root(void);

// Inserts an existing node (e.g. a device) into a ramfs directory under `name`.
// The directory takes its own reference on the node.
int ramfs_add_node(struct vfs_node *dir, const char *name, struct vfs_node *node);

#endif