#ifndef KERNEL_FBDEV_H
#define KERNEL_FBDEV_H

#include "vfs.h"

// The /dev/fb0 device node or NULL if the machine booted without a framebuffer
struct vfs_node *fbdev_node(void);

#endif