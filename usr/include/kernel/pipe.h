#ifndef KERNEL_PIPE_H
#define KERNEL_PIPE_H

#include "vfs.h"

// Creates a pipe; *rd and *wr are open files (one reference each) for its two ends
int pipe_create(struct vfs_file **rd, struct vfs_file **wr);

#endif