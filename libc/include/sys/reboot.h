#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H
#define RB_AUTOBOOT 0x01234567
#define RB_HALT     0xCDEF0123
#define RB_POWEROFF 0x4321FEDC
int reboot(int);
#endif