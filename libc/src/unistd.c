#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <termios.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/reboot.h>
#include <sys/uio.h>
#include "internal.h"

#define SC3(n, a, b, c) __sys_ret(__syscall3((n), (long)(a), (long)(b), (long)(c)))

// ---------- files ---------- //

ssize_t read(int fd, void *buf, size_t n)         { return (ssize_t)SC3(SYS_read, fd, buf, n); }
ssize_t write(int fd, const void *buf, size_t n)  { return (ssize_t)SC3(SYS_write, fd, buf, n); }
int close(int fd)                                  { return (int)SC3(SYS_close, fd, 0, 0); }
off_t lseek(int fd, off_t off, int whence)         { return (off_t)SC3(SYS_lseek, fd, off, whence); }
int dup(int fd)                                    { return (int)SC3(SYS_dup, fd, 0, 0); }
int dup2(int a, int b)                             { return (int)SC3(SYS_dup2, a, b, 0); }
int pipe(int fds[2])                               { return (int)SC3(SYS_pipe, fds, 0, 0); }
int access(const char *p, int mode)                { return (int)SC3(SYS_access, p, mode, 0); }
int chdir(const char *p)                           { return (int)SC3(SYS_chdir, p, 0, 0); }
int rmdir(const char *p)                           { return (int)SC3(SYS_rmdir, p, 0, 0); }
int unlink(const char *p)                          { return (int)SC3(SYS_unlink, p, 0, 0); }
int rename(const char *a, const char *b)           { return (int)SC3(SYS_rename, a, b, 0); }
int chown(const char *p, uid_t u, gid_t g)         { return (int)SC3(SYS_chown, p, u, g); }
int chmod(const char *p, mode_t m)                 { return (int)SC3(SYS_chmod, p, m, 0); }
int mkdir(const char *p, mode_t m)                 { (void)m; return (int)SC3(SYS_mkdir, p, 0, 0); }
mode_t umask(mode_t m)                             { return (mode_t)SC3(SYS_umask, m, 0, 0); }
void sync(void)                                    { SC3(SYS_sync, 0, 0, 0); }

int creat(const char *path, mode_t m)              { (void)m; return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0); }

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    return (int)SC3(SYS_fcntl, fd, cmd, arg);
}

int ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    long arg = va_arg(ap, long);
    va_end(ap);
    return (int)SC3(SYS_ioctl, fd, req, arg);
}

ssize_t pread(int fd, void *buf, size_t n, off_t off) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0 || lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = read(fd, buf, n);
    lseek(fd, cur, SEEK_SET);
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0 || lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = write(fd, buf, n);
    lseek(fd, cur, SEEK_SET);
    return r;
}

ssize_t readv(int fd, const struct iovec *v, int n) {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        ssize_t r = read(fd, v[i].iov_base, v[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if ((size_t)r < v[i].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *v, int n) {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        ssize_t r = write(fd, v[i].iov_base, v[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if ((size_t)r < v[i].iov_len) break;
    }
    return total;
}

char *getcwd(char *buf, size_t size) {
    int own = 0;

    if (!buf) {
        size = size ? size : 512;
        buf = malloc(size);
        if (!buf) return NULL;
        own = 1;
    }
    long r = SC3(SYS_getcwd, buf, size, 0);
    if (r < 0) {
        if (own) free(buf);
        return NULL;
    }
    return buf;
}

// the kernel's stat structure is smaller than POSIX's
struct kstat { unsigned int ino, mode, uid, gid, size; };

static void conv_stat(struct stat *st, const struct kstat *k) {
    memset(st, 0, sizeof(*st));
    st->st_ino     = k->ino;
    st->st_mode    = k->mode;
    st->st_uid     = k->uid;
    st->st_gid     = k->gid;
    st->st_size    = (off_t)k->size;
    st->st_nlink   = 1;
    st->st_blksize = 4096;
    st->st_blocks  = (blkcnt_t)((k->size + 511) / 512);
}

int stat(const char *p, struct stat *st) {
    struct kstat k;
    if (SC3(SYS_stat, p, &k, 0) < 0) return -1;
    conv_stat(st, &k);
    return 0;
}

int lstat(const char *p, struct stat *st) {
    struct kstat k;
    if (SC3(SYS_lstat, p, &k, 0) < 0) return -1;
    conv_stat(st, &k);
    return 0;
}

int fstat(int fd, struct stat *st) {
    struct kstat k;
    if (SC3(SYS_fstat, fd, &k, 0) < 0) return -1;
    conv_stat(st, &k);
    return 0;
}

int mkfifo(const char *p, mode_t m)         { (void)p; (void)m; errno = ENOSYS; return -1; }
int link(const char *a, const char *b)      { (void)a; (void)b; errno = ENOSYS; return -1; }
int symlink(const char *a, const char *b)   { (void)a; (void)b; errno = ENOSYS; return -1; }
ssize_t readlink(const char *p, char *b, size_t n) { (void)p; (void)b; (void)n; errno = EINVAL; return -1; }
int fsync(int fd)                           { (void)fd; return 0; }
int fdatasync(int fd)                       { (void)fd; return 0; }

// ---------- processes ---------- //

pid_t fork(void)                            { return (pid_t)SC3(SYS_fork, 0, 0, 0); }
pid_t getpid(void)                          { return (pid_t)SC3(SYS_getpid, 0, 0, 0); }
pid_t getppid(void)                         { return (pid_t)SC3(SYS_getppid, 0, 0, 0); }
uid_t getuid(void)                          { return (uid_t)SC3(SYS_getuid, 0, 0, 0); }
uid_t geteuid(void)                         { return (uid_t)SC3(SYS_geteuid, 0, 0, 0); }
gid_t getgid(void)                          { return (gid_t)SC3(SYS_getgid, 0, 0, 0); }
gid_t getegid(void)                         { return (gid_t)SC3(SYS_getegid, 0, 0, 0); }
int setuid(uid_t u)                         { return (int)SC3(SYS_setuid, u, 0, 0); }
int setgid(gid_t g)                         { return (int)SC3(SYS_setgid, g, 0, 0); }
int setpgid(pid_t p, pid_t g)               { return (int)SC3(SYS_setpgid, p, g, 0); }
pid_t getpgrp(void)                         { return (pid_t)SC3(SYS_getpgrp, 0, 0, 0); }
pid_t getpgid(pid_t p)                      { return (pid_t)SC3(SYS_getpgid, p, 0, 0); }
int kill(pid_t pid, int sig)                { return (int)SC3(SYS_kill, pid, sig, 0); }
pid_t waitpid(pid_t pid, int *st, int opt)  { return (pid_t)SC3(SYS_waitpid, pid, st, opt); }
pid_t wait(int *st)                         { return waitpid(-1, st, 0); }

void _exit(int code) {
    for (;;) __syscall3(SYS_exit, code, 0, 0);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)SC3(SYS_execve, path, argv, envp);
}

int execv(const char *path, char *const argv[]) {
    return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
    if (strchr(file, '/')) return execve(file, argv, environ);

    const char *path = getenv("PATH");
    if (!path) path = "/bin:/sbin";
    size_t fl = strlen(file);
    int saw_eacces = 0;

    while (*path) {
        const char *e = strchr(path, ':');
        size_t dl = e ? (size_t)(e - path) : strlen(path);
        char full[512];

        if (dl + 1 + fl + 1 <= sizeof(full)) {
            memcpy(full, path, dl);
            full[dl] = '/';
            memcpy(full + dl + 1, file, fl + 1);
            execve(full, argv, environ);
            if (errno == EACCES) saw_eacces = 1;
        }
        if (!e) break;
        path = e + 1;
    }
    errno = saw_eacces ? EACCES : ENOENT;
    return -1;
}

static int exec_va(const char *path, const char *arg0, va_list ap, int search) {
    char *argv[64];
    int n = 0;

    argv[n++] = (char *)arg0;
    while (n < 63 && (argv[n] = va_arg(ap, char *)) != NULL) n++;
    argv[n] = NULL;
    return search ? execvp(path, argv) : execve(path, argv, environ);
}

int execl(const char *path, const char *arg0, ...) {
    va_list ap;
    va_start(ap, arg0);
    int r = exec_va(path, arg0, ap, 0);
    va_end(ap);
    return r;
}

int execlp(const char *file, const char *arg0, ...) {
    va_list ap;
    va_start(ap, arg0);
    int r = exec_va(file, arg0, ap, 1);
    va_end(ap);
    return r;
}

// ---------- time and sleeping ---------- //

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return (int)SC3(SYS_nanosleep, req, rem, 0);
}

unsigned int sleep(unsigned int s) {
    struct timespec ts = { (time_t)s, 0 };
    return nanosleep(&ts, NULL) < 0 ? s : 0;
}

int usleep(useconds_t us) {
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    return nanosleep(&ts, NULL);
}

unsigned int alarm(unsigned int s)  { (void)s; return 0; }       // no timers yet

int pause(void) {
    for (;;) sleep(3600);
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tz) { tz->tz_minuteswest = 0; tz->tz_dsttime = 0; }
    return (int)SC3(SYS_gettimeofday, tv, 0, 0);
}

int clock_gettime(int clock, struct timespec *ts) {
    return (int)SC3(SYS_clock_gettime, clock, ts, 0);
}

// ---------- memory ---------- //

int brk(void *addr) {
    long r = __syscall3(SYS_brk, (long)addr, 0, 0);
    if ((void *)r != addr) { errno = ENOMEM; return -1; }
    return 0;
}

void *sbrk(ptrdiff_t inc) {
    long cur = __syscall3(SYS_brk, 0, 0, 0);

    if (inc == 0) return (void *)cur;
    long want = __syscall3(SYS_brk, cur + inc, 0, 0);
    if (want != cur + inc) { errno = ENOMEM; return (void *)-1; }
    return (void *)cur;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    long r = __syscall6(SYS_mmap2, (long)addr, (long)len, prot, flags, fd, off >> 12);
    if ((unsigned long)r > (unsigned long)-4096L) { errno = (int)-r; return MAP_FAILED; }
    return (void *)r;
}

int munmap(void *addr, size_t len)              { return (int)SC3(SYS_munmap, addr, len, 0); }
int mprotect(void *a, size_t l, int p)          { (void)a; (void)l; (void)p; return 0; }
int msync(void *a, size_t l, int f)             { (void)a; (void)l; (void)f; return 0; }

// ---------- polling ---------- //

int poll(struct pollfd *fds, unsigned int n, int timeout) {
    return (int)SC3(SYS_poll, fds, n, timeout);
}

int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv) {
    return (int)__sys_ret(__syscall6(SYS_select, n, (long)r, (long)w, (long)e, (long)tv, 0));
}

// ---------- terminals ---------- //

int tcgetattr(int fd, struct termios *t)                   { return ioctl(fd, 0x5401, t); }
int tcsetattr(int fd, int act, const struct termios *t)    { return ioctl(fd, 0x5402 + act, t); }
int isatty(int fd)                                         { struct termios t; return tcgetattr(fd, &t) == 0; }

char *ttyname(int fd) {
    return isatty(fd) ? "/dev/console" : NULL;
}

void cfmakeraw(struct termios *t) {
    t->c_iflag &= ~(unsigned)(ICRNL | IXON);
    t->c_oflag &= ~(unsigned)OPOST;
    t->c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

speed_t cfgetispeed(const struct termios *t)       { (void)t; return B38400; }
speed_t cfgetospeed(const struct termios *t)       { (void)t; return B38400; }
int cfsetispeed(struct termios *t, speed_t s)      { (void)t; (void)s; return 0; }
int cfsetospeed(struct termios *t, speed_t s)      { (void)t; (void)s; return 0; }
int tcflush(int fd, int q)                         { (void)fd; (void)q; return 0; }
int tcdrain(int fd)                                { (void)fd; return 0; }

pid_t tcgetpgrp(int fd) {
    int pg;
    return ioctl(fd, TIOCGPGRP, &pg) < 0 ? -1 : pg;
}

int tcsetpgrp(int fd, pid_t pg) {
    return ioctl(fd, TIOCSPGRP, &pg);
}

// ---------- system information ---------- //

int uname(struct utsname *u)                        { return (int)SC3(SYS_uname, u, 0, 0); }
int reboot(int cmd)                                 { return (int)SC3(SYS_reboot, cmd, 0, 0); }
int sethostname(const char *n, size_t len)          { return (int)SC3(SYS_sethostname, n, len, 0); }

int gethostname(char *name, size_t len) {
    struct utsname u;
    if (uname(&u) < 0) return -1;
    if (strlen(u.nodename) >= len) { errno = ENAMETOOLONG; return -1; }
    strcpy(name, u.nodename);
    return 0;
}

long sysconf(int name) {
    switch (name) {
    case _SC_ARG_MAX:          return 3072;
    case _SC_CHILD_MAX:        return 32;
    case _SC_CLK_TCK:          return 100;
    case _SC_OPEN_MAX:         return 32;
    case _SC_PAGESIZE:         return 4096;
    case _SC_NPROCESSORS_ONLN: return 1;
    default:                   errno = EINVAL; return -1;
    }
}

int getpagesize(void) { return 4096; }