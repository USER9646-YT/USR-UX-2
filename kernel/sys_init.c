#include "syscall.h"

void sys_extra_init(void);      // kernel/sys_extra.c: sethostname, fcntl
void sys_poll_init(void);       // kernel/poll.c: poll, select
void sys_cred_init(void);       // kernel/sys_cred.c: users, groups, process groups
void sys_tty_init(void);        // kernel/sys_tty.c: ioctl (console, termios, graphics mode)
void sys_mem_init(void);        // kernel/sys_mem.c: brk
void sys_time_init(void);       // kernel/sys_time.c: time, gettimeofday, clock_gettime
void sys_misc_init(void);       // kernel/sys_misc.c: fstat, lstat, access, umask
void sys_socket_init(void);     // kernel/sys_socket.c: socketcall (AF_UNIX sockets)

void syscall_register_all(void) {
    sys_extra_init();
    sys_poll_init();
    sys_cred_init();
    sys_tty_init();
    sys_mem_init();
    sys_time_init();
    sys_misc_init();
    sys_socket_init();
}