#include <stdint.h>
#include <stdarg.h>
#include "panic.h"
#include "paging.h"
#include "terminal.h"
#include "kprintf.h"
#include "syscall.h"

void kpanic(const char *fmt, ...) {
    __asm__ volatile ("cli");

    paging_switch(paging_kernel_dir());      // run on the kernel's own page tables
    sys_tty_reset_console();                 // a graphics program may own the screen

    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\npanic: ");

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);

    kprintf("\nSystem halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}