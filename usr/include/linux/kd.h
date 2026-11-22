#ifndef _LINUX_KD_H
#define _LINUX_KD_H

#define KDGKBTYPE 0x4B33        // keyboard type (char *)
#define KDSETMODE 0x4B3A
#define KDGETMODE 0x4B3B
#define KDGKBMODE 0x4B44        // keyboard mode (int *)
#define KDSKBMODE 0x4B45        // set it (value)

#define KD_TEXT     0
#define KD_GRAPHICS 1

#define KB_84    0x01
#define KB_101   0x02
#define KB_OTHER 0x03

#define K_RAW       0x00
#define K_XLATE     0x01        // translated characters: the only mode this kernel has
#define K_MEDIUMRAW 0x02
#define K_UNICODE   0x03
#define K_OFF       0x04

#endif