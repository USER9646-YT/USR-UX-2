#ifndef KERNEL_PTY_H
#define KERNEL_PTY_H

#include "vfs.h"

// /dev/ptmx: opening it creates a new pty and returns its master side
struct vfs_node *pty_ptmx_node(void);
// /dev/pts/: a directory with one entry per live pty (the slave sides)
struct vfs_node *pty_pts_node(void);

#endif