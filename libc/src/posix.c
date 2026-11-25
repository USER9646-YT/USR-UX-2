#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <sys/wait.h>

long __syscall6(long, long, long, long, long, long, long);   // asm.s

static long ret(long r) {
    if ((unsigned long)r > (unsigned long)-4096L) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

#define SC(n, a, b, c, d, e) ret(__syscall6((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0))

pid_t wait4(pid_t pid, int *status, int options, struct rusage *ru) {
    return (pid_t)SC(114, pid, status, options, ru, 0);
}

pid_t vfork(void)                    { return (pid_t)SC(190, 0, 0, 0, 0, 0); }
int   sched_yield(void)              { return (int)SC(158, 0, 0, 0, 0, 0); }
pid_t getsid(pid_t pid)              { return (pid_t)SC(147, pid, 0, 0, 0, 0); }
int   sysinfo(struct sysinfo *si)    { return (int)SC(116, si, 0, 0, 0, 0); }
clock_t times(struct tms *buf)       { return (clock_t)SC(43, buf, 0, 0, 0, 0); }
int   getrusage(int who, struct rusage *ru) { return (int)SC(77, who, ru, 0, 0, 0); }

int getrlimit(int resource, struct rlimit *rl) { return (int)SC(191, resource, rl, 0, 0, 0); }
int setrlimit(int resource, const struct rlimit *rl) { return (int)SC(75, resource, rl, 0, 0, 0); }

int nice(int inc)                    { return (int)SC(34, inc, 0, 0, 0, 0); }

int getpriority(int which, id_t who) {
    long r = SC(96, which, who, 0, 0, 0);
    return r < 0 ? -1 : (int)(20 - r);               // the kernel returns 20 - nice
}

int setpriority(int which, id_t who, int prio) { return (int)SC(97, which, who, prio, 0, 0); }

int getgroups(int size, gid_t *list)  { return (int)SC(80, size, list, 0, 0, 0); }
int setgroups(size_t size, const gid_t *list) { return (int)SC(81, size, list, 0, 0, 0); }

int setreuid(uid_t r, uid_t e)        { return (int)SC(70, r, e, 0, 0, 0); }
int setregid(gid_t r, gid_t e)        { return (int)SC(71, r, e, 0, 0, 0); }
int setresuid(uid_t r, uid_t e, uid_t s) { return (int)SC(164, r, e, s, 0, 0); }
int setresgid(gid_t r, gid_t e, gid_t s) { return (int)SC(170, r, e, s, 0, 0); }
int getresuid(uid_t *r, uid_t *e, uid_t *s) { return (int)SC(165, r, e, s, 0, 0); }
int getresgid(gid_t *r, gid_t *e, gid_t *s) { return (int)SC(171, r, e, s, 0, 0); }

int truncate(const char *path, off_t len)  { return (int)SC(92, path, len, 0, 0, 0); }
int ftruncate(int fd, off_t len)           { return (int)SC(93, fd, len, 0, 0, 0); }
int fchmod(int fd, mode_t mode)            { return (int)SC(94, fd, mode, 0, 0, 0); }
int fchown(int fd, uid_t u, gid_t g)       { return (int)SC(95, fd, u, g, 0, 0); }
int lchown(const char *path, uid_t u, gid_t g) { return (int)SC(198, path, u, g, 0, 0); }

// dup3 and pipe2: close-on-exec is not implemented, so O_CLOEXEC does nothing yet
int dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) { errno = EINVAL; return -1; }
    (void)flags;
    return dup2(oldfd, newfd);
}

int pipe2(int fds[2], int flags) {
    int r = pipe(fds);
    if (r == 0 && (flags & O_NONBLOCK)) {
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        fcntl(fds[1], F_SETFL, O_NONBLOCK);
    }
    return r;
}