#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include "vfs.h"

void mouse_init(void);                      // PS/2 mouse on IRQ 12; call after pic_remap()
struct vfs_node *mouse_node(void);          // /dev/input/mice, /dev/psaux, /dev/mouse

#endif