#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/procinfo.h>
#include <sys/reboot.h>
#include <time.h>
#include <dirent.h>

#define SYS_exit      1
#define SYS_fork      2
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_waitpid   7
#define SYS_unlink   10
#define SYS_execve   11
#define SYS_chdir    12
#define SYS_chmod    15
#define SYS_chown    16
#define SYS_lseek    19
#define SYS_getpid   20
#define SYS_setuid   23
#define SYS_getuid   24
#define SYS_sync     36
#define SYS_kill     37
#define SYS_rename   38
#define SYS_mkdir    39
#define SYS_rmdir    40
#define SYS_dup      41
#define SYS_pipe     42
#define SYS_setgid   46
#define SYS_getgid   47
#define SYS_geteuid  49
#define SYS_ioctl    54
#define SYS_setpgid  57
#define SYS_dup2     63
#define SYS_getppid  64
#define SYS_getpgrp  65
#define SYS_reboot   88
#define SYS_readdir  89
#define SYS_stat    106
#define SYS_uname   122
#define SYS_nanosleep 162
#define SYS_getcwd  183
#define SYS_procinfo 400

#define WNOHANG 1
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)

static inline int __syscall3(int n, int a, int b, int c) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return ret;
}

static inline int write(int fd, const void *buf, unsigned int len) {
    return __syscall3(SYS_write, fd, (int)buf, (int)len);
}
static inline int read(int fd, void *buf, unsigned int len) {
    return __syscall3(SYS_read, fd, (int)buf, (int)len);
}
static inline int open(const char *path, int flags) {
    return __syscall3(SYS_open, (int)path, flags, 0);
}
static inline int close(int fd) {
    return __syscall3(SYS_close, fd, 0, 0);
}
static inline int lseek(int fd, int off, int whence) {
    return __syscall3(SYS_lseek, fd, off, whence);
}
static inline int dup(int fd) {
    return __syscall3(SYS_dup, fd, 0, 0);
}
static inline int dup2(int oldfd, int newfd) {
    return __syscall3(SYS_dup2, oldfd, newfd, 0);
}
static inline int pipe(int fds[2]) {
    return __syscall3(SYS_pipe, (int)fds, 0, 0);
}
static inline int stat(const char *path, struct stat *st) {
    return __syscall3(SYS_stat, (int)path, (int)st, 0);
}
static inline int readdir(int fd, struct dirent *d) {       /* 1 = got an entry, 0 = end */
    return __syscall3(SYS_readdir, fd, (int)d, 0);
}
static inline int chdir(const char *path) {
    return __syscall3(SYS_chdir, (int)path, 0, 0);
}
static inline int getcwd(char *buf, unsigned int size) {    /* length incl. NUL, or -errno */
    return __syscall3(SYS_getcwd, (int)buf, (int)size, 0);
}
static inline int mkdir(const char *path, unsigned int mode) {
    return __syscall3(SYS_mkdir, (int)path, (int)mode, 0);
}
static inline int rmdir(const char *path) {
    return __syscall3(SYS_rmdir, (int)path, 0, 0);
}
static inline int unlink(const char *path) {
    return __syscall3(SYS_unlink, (int)path, 0, 0);
}
static inline int rename(const char *from, const char *to) {
    return __syscall3(SYS_rename, (int)from, (int)to, 0);
}
static inline int uname(struct utsname *u) {
    return __syscall3(SYS_uname, (int)u, 0, 0);
}
static inline int chmod(const char *path, unsigned int mode) {
    return __syscall3(SYS_chmod, (int)path, (int)mode, 0);
}
static inline int chown(const char *path, int uid, int gid) {   /* -1 = leave unchanged */
    return __syscall3(SYS_chown, (int)path, uid, gid);
}
static inline int getuid(void) {                            /* the real user: who started us */
    return __syscall3(SYS_getuid, 0, 0, 0);
}
static inline int geteuid(void) {                           /* the effective user: used for permission checks */
    return __syscall3(SYS_geteuid, 0, 0, 0);
}
static inline int getgid(void) {
    return __syscall3(SYS_getgid, 0, 0, 0);
}
static inline int setuid(unsigned int uid) {
    return __syscall3(SYS_setuid, (int)uid, 0, 0);
}
static inline int setgid(unsigned int gid) {
    return __syscall3(SYS_setgid, (int)gid, 0, 0);
}
static inline int setpgid(int pid, int pgid) {              /* 0 = this process / its own pid */
    return __syscall3(SYS_setpgid, pid, pgid, 0);
}
static inline int getpgrp(void) {
    return __syscall3(SYS_getpgrp, 0, 0, 0);
}
static inline int ioctl(int fd, unsigned int req, unsigned int arg) {
    return __syscall3(SYS_ioctl, fd, (int)req, (int)arg);
}
static inline int fork(void) {
    return __syscall3(SYS_fork, 0, 0, 0);
}
static inline int getpid(void) {
    return __syscall3(SYS_getpid, 0, 0, 0);
}
static inline int getppid(void) {
    return __syscall3(SYS_getppid, 0, 0, 0);
}
static inline int waitpid(int pid, int *status, int options) {
    return __syscall3(SYS_waitpid, pid, (int)status, options);
}
static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    return __syscall3(SYS_execve, (int)path, (int)argv, (int)envp);
}
static inline int kill(int pid, int sig) {
    return __syscall3(SYS_kill, pid, sig, 0);
}
static inline int nanosleep(const struct timespec *req, struct timespec *rem) {
    return __syscall3(SYS_nanosleep, (int)req, (int)rem, 0);
}
static inline int sleep(unsigned int seconds) {
    struct timespec ts = { (int)seconds, 0 };
    return nanosleep(&ts, 0);
}
static inline int sync(void) {
    return __syscall3(SYS_sync, 0, 0, 0);
}
static inline int reboot(unsigned int cmd) {                 /* only returns on error */
    return __syscall3(SYS_reboot, (int)cmd, 0, 0);
}
static inline int procinfo(int index, struct procinfo *pi) { /* 1 = filled, 0 = empty slot, <0 = end */
    return __syscall3(SYS_procinfo, index, (int)pi, 0);
}
static inline void _exit(int code) {
    __syscall3(SYS_exit, code, 0, 0);
    for (;;) ;
}

#endif /* _UNISTD_H */