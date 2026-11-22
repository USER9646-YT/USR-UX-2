#ifndef KERNEL_POWER_H
#define KERNEL_POWER_H

void power_reboot(void) __attribute__((noreturn));
void power_off(void)    __attribute__((noreturn));
void power_halt(void)   __attribute__((noreturn));

#endif