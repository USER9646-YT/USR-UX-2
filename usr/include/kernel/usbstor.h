#ifndef KERNEL_USBSTOR_H
#define KERNEL_USBSTOR_H

#include <stdint.h>

struct usbstor_disk {
    int      present;
    uint32_t sectors;       // total 512-byte sectors
    char     model[41];
};

int  usbstor_init(void);                                    // returns the number of USB disks
const struct usbstor_disk *usbstor_get(int idx);
int  usbstor_read (int idx, uint32_t lba, uint32_t count, void *buf);        // 0 = ok
int  usbstor_write(int idx, uint32_t lba, uint32_t count, const void *buf);  // 0 = ok

#endif