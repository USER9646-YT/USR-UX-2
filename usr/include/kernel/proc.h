#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include <stdint.h>
#include <stddef.h>
#include <sys/procinfo.h>
#include "idt.h"
#include "vfs.h"

enum proc_state {
    P_UNUSED = 0,
    P_EMBRYO,        // being created
    P_RUNNABLE,
    P_RUNNING,
    P_SLEEP_TTY,     // waiting for keyboard / mouse input
    P_SLEEP_WAIT,    // waiting for a child to exit
    P_SLEEP_PIPE,    // waiting for data or room in a pipe
    P_SLEEP_TIMER,   // sleeping until wake_tick
    P_ZOMBIE         // exited, waiting to be reaped
};

struct process {
    int              pid;
    int              ppid;
    int              pgid;                  // process group (for Ctrl-C and job control)
    enum proc_state  state;
    uint32_t         dir;                   // physical address of the page directory
    uint8_t         *kstack;                // kernel stack (heap allocation)
    uint32_t         ksp;                   // saved kernel stack pointer
    int              exit_code;
    uint32_t         uid;                   // EFFECTIVE user: used for permission checks (0 = root)
    uint32_t         ruid;                  // realest of the real user: who started the program
    uint32_t         gid;
    char             name[16];              // program name, for ps / pkill
    int              killed;                // pending fatal signal (0 = none)
    uint32_t         wake_tick;             // for P_SLEEP_TIMER
    uint32_t         brk_start;             // the heap: [brk_start, brk_end)
    uint32_t         brk_end;
    struct vfs_file *fds[VFS_MAX_FDS];      // open files; 0, 1, 2 = console
    char             cwd[VFS_PATH_MAX];     // current directory (absolute)
};

extern struct process *current;

int  proc_spawn_init(const char *path);
void proc_start(void) __attribute__((noreturn));

void schedule(void);
void yield(void);
void sched_sleep(enum proc_state why);
void sched_wake(enum proc_state why);

int  proc_fork(struct registers *r);
int  proc_exec(struct registers *r, const char *path, char *const *argv, char *const *envp);
int  proc_waitpid(int pid, int *status, int options);     // options: 1 = WNOHANG
void proc_exit(int code) __attribute__((noreturn));

int  proc_signal(int pid, int sig);            // with permission checks; 0 or -errno
int  proc_kill_group(int pgid, int sig);       // kernel-originated (Ctrl-C): returns how many were signalled
int  proc_setpgid(int pid, int pgid);          // pid/pgid 0 = this process / its own pid
int  proc_getpgid(int pid);                    // 0 = this process; -ESRCH if there is none
void proc_tick(uint32_t ticks);                // called from the timer interrupt
int  proc_sleep(uint32_t nticks);              // 0, or -EINTR if a signal arrived
int  proc_info(int index, struct procinfo *out);
int  proc_cwd_inside(const char *prefix);      // 1 if any process's cwd is at or below prefix

// Copies a NUL-terminated string from user space; returns its length or -errno
int  copy_str_from_user(char *dst, const char *src, size_t max);

#endif