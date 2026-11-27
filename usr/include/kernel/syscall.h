#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>
#include "idt.h"

// Moved the uname definitions here
#define OS_SYSNAME  "USR/UX 2"
#define OS_RELEASE  "0.97b"
#define OS_VERSION    "#1 " __DATE__ " " __TIME__
#define OS_MACHINE  "i386"

// A syscall implemented outside syscall.c: its arguments are in r->ebx, ecx, edx, esi, edi, ebp,
// and the return value is the result (or -errno). Registered syscalls take priority over the
// built-in ones with the same number.
typedef int (*syscall_fn)(struct registers *r);

void syscall_init(void);
int  syscall_register(uint32_t num, syscall_fn fn);     // 0, or -1 if the table is full
void syscall_register_all(void);                        // kernel/sys_init.c

// Helpers for syscall files
int  syscall_user_path(const char *upath, char *out);   // user string -> absolute path (VFS_PATH_MAX bytes)

// the console / graphics-mode part (kernel/sys_tty.c)
void sys_tty_hook(void);                                // runs at the start of every syscall
void sys_tty_reset_console(void);                       // gives the screen back to the text console

#endif