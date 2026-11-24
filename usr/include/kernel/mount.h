#ifndef KERNEL_MOUNT_H
#define KERNEL_MOUNT_H

#include <stdint.h>
#include <stddef.h>

void sys_mount_init(void);                                           // registers mount (21) and umount (22)

// Records a block-device mount made by the kernel itself (the root filesystem at boot)
void mount_record(const char *target, int disk, uint32_t start, uint32_t sectors);

// If `target` is a mount of a block device: 0 and its name ("disk0p1") in dev
int  mount_device(const char *target, char *dev, size_t size);

#endif