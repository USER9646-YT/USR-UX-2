#ifndef KERNEL_KBDMENU_H
#define KERNEL_KBDMENU_H

#include "multiboot.h"

// Chooses the keyboard layout: kbd=<name> on the kernel command line picks one directly,
// otherwise a menu is shown (it continues with the first layout after a few seconds if
// nobody touches the keyboard). Must run before interrupts are enabled.
void kbdmenu_run(const struct multiboot_info *mbi);

#endif