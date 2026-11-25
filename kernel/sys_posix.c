#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/procinfo.h>
#include "syscall.h"
#include "proc.h"
#include "vfs.h"
#include "paging.h"
#include "pmm.h"
#include "pit.h"

#define NOCHANGE      0xFFFFFFFFu       // "-1" in a set*id call: leave it as it is
#define RLIM_INFINITY 0xFFFFFFFFu

static int fd_ok(int fd) {
    return fd >= 0 && fd < VFS_MAX_FDS && current->fds[fd] != NULL;
}

static int user_r(const void *p, uint32_t n) { return paging_check_user((uint32_t)(uintptr_t)p, n, 0); }
static int user_w(const void *p, uint32_t n) { return paging_check_user((uint32_t)(uintptr_t)p, n, 1); }

// ---------- processes ---------- //

// wait4(pid, status, options, rusage): 114. Resource usage is not tracked: it reads as zero.
static int sys_wait4(struct registers *r) {
    int pid = (int)r->ebx;
    int *ustatus = (int *)(uintptr_t)r->ecx;
    int options = (int)r->edx;
    void *ru = (void *)(uintptr_t)r->esi;
    int status = 0;

    if (ustatus && !user_w(ustatus, sizeof(int))) return -EFAULT;
    if (ru && !user_w(ru, 72)) return -EFAULT;

    int res = proc_waitpid(pid, &status, options);
    if (res > 0) {
        if (ustatus) *ustatus = status;
        if (ru) memset(ru, 0, 72);
    }
    return res;
}

// vfork(): 190. A full fork: the child gets its own copy of the address space.
static int sys_vfork(struct registers *r) {
    return proc_fork(r);
}

// sched_yield(): 158
static int sys_sched_yield(struct registers *r) {
    (void)r;
    yield();
    return 0;
}

// getsid(pid): 147. There are no sessions: a session is the process group setsid() made.
static int sys_getsid(struct registers *r) {
    return proc_getpgid((int)r->ebx);
}

// ---------- system information ---------- //

struct ksysinfo {                   // 64 bytes, like Linux i386
    int32_t  uptime;
    uint32_t loads[3];
    uint32_t totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    uint16_t procs, pad;
    uint32_t totalhigh, freehigh;
    uint32_t mem_unit;
    char     f[8];
};

// sysinfo(info): 116
static int sys_sysinfo(struct registers *r) {
    struct ksysinfo *u = (struct ksysinfo *)(uintptr_t)r->ebx;
    struct ksysinfo si;
    struct procinfo pi;
    uint32_t hz = pit_freq() ? pit_freq() : 100;

    if (!user_w(u, sizeof(*u))) return -EFAULT;

    memset(&si, 0, sizeof(si));
    si.uptime   = (int32_t)(pit_ticks() / hz);
    si.totalram = pmm_total_frames() * PAGE_SIZE;
    si.freeram  = pmm_free_frames() * PAGE_SIZE;
    si.mem_unit = 1;
    for (int i = 0; i < 256; i++) {
        int res = proc_info(i, &pi);
        if (res < 0) break;
        if (res > 0) si.procs++;
    }
    memcpy(u, &si, sizeof(si));
    return 0;
}

// times(buf): 43. Per-process CPU time is not tracked: the fields read as zero, and the
// return value is the time since boot in 1/100 s.
static int sys_times(struct registers *r) {
    uint32_t *t = (uint32_t *)(uintptr_t)r->ebx;
    uint32_t hz = pit_freq() ? pit_freq() : 100;
    uint32_t ticks = pit_ticks();

    if (t) {
        if (!user_w(t, 16)) return -EFAULT;
        memset(t, 0, 16);
    }
    return (int)(ticks / hz * 100u + (ticks % hz) * 100u / hz);
}

// getrusage(who, usage): 77
static int sys_getrusage(struct registers *r) {
    void *u = (void *)(uintptr_t)r->ecx;
    if (!user_w(u, 72)) return -EFAULT;
    memset(u, 0, 72);
    return 0;
}

// ---------- resource limits ---------- //

static int rlimit_value(uint32_t res, uint32_t *cur, uint32_t *max) {
    if (res >= 16) return -EINVAL;
    switch (res) {
    case 3:  *cur = *max = 64u * 4096u;     break;      // RLIMIT_STACK: USER_STACK_PAGES in proc.c
    case 7:  *cur = *max = VFS_MAX_FDS;     break;      // RLIMIT_NOFILE
    default: *cur = *max = RLIM_INFINITY;   break;
    }
    return 0;
}

// getrlimit (76, old) and ugetrlimit (191)
static int sys_getrlimit(struct registers *r) {
    uint32_t *u = (uint32_t *)(uintptr_t)r->ecx;
    uint32_t cur, max;
    int err = rlimit_value(r->ebx, &cur, &max);

    if (err) return err;
    if (!user_w(u, 8)) return -EFAULT;
    u[0] = cur;
    u[1] = max;
    return 0;
}

// setrlimit(resource, rlim): 75. Accepted and ignored: the limits are fixed.
static int sys_setrlimit(struct registers *r) {
    const uint32_t *u = (const uint32_t *)(uintptr_t)r->ecx;
    uint32_t cur, max;

    if (rlimit_value(r->ebx, &cur, &max)) return -EINVAL;
    if (!user_r(u, 8)) return -EFAULT;
    if (u[0] > u[1]) return -EINVAL;
    return 0;
}

// nice(inc): 34, getpriority(which, who): 96, setpriority(which, who, prio): 97.
// There is one scheduling priority for everybody; these exist so that nice and renice run.
static int sys_nice(struct registers *r) {
    if ((int)r->ebx < 0 && current->uid != 0) return -EPERM;
    return 0;
}
static int sys_getpriority(struct registers *r) { (void)r; return 20; }      // 20 - nice, nice = 0
static int sys_setpriority(struct registers *r) { (void)r; return 0; }

// ---------- users and groups ---------- //

// getgroups(size, list): 80. Everybody belongs to exactly one group.
static int sys_getgroups(struct registers *r) {
    int size = (int)r->ebx;
    uint32_t *list = (uint32_t *)(uintptr_t)r->ecx;

    if (size < 0) return -EINVAL;
    if (size == 0) return 1;
    if (!user_w(list, 4)) return -EFAULT;
    list[0] = current->gid;
    return 1;
}

// setgroups(size, list): 81. Root only, and ignored (there is only the primary group).
static int sys_setgroups(struct registers *r) {
    (void)r;
    return current->uid == 0 ? 0 : -EPERM;
}

static int may_become(uint32_t v, uint32_t a, uint32_t b) {
    return v == NOCHANGE || v == a || v == b;
}

// setreuid(ruid, euid): 70
static int sys_setreuid(struct registers *r) {
    uint32_t ru = r->ebx, eu = r->ecx;

    if (current->uid != 0 && (!may_become(ru, current->ruid, current->uid) ||
                              !may_become(eu, current->ruid, current->uid)))
        return -EPERM;
    if (ru != NOCHANGE) current->ruid = ru;
    if (eu != NOCHANGE) current->uid  = eu;
    vfs_set_creds(current->uid, current->gid);
    return 0;
}

// setresuid(ruid, euid, suid): 164. There is no saved user id: it is only checked.
static int sys_setresuid(struct registers *r) {
    uint32_t ru = r->ebx, eu = r->ecx, su = r->edx;

    if (current->uid != 0 && (!may_become(ru, current->ruid, current->uid) ||
                              !may_become(eu, current->ruid, current->uid) ||
                              !may_become(su, current->ruid, current->uid)))
        return -EPERM;
    if (ru != NOCHANGE) current->ruid = ru;
    if (eu != NOCHANGE) current->uid  = eu;
    vfs_set_creds(current->uid, current->gid);
    return 0;
}

// getresuid(&ruid, &euid, &suid): 165
static int sys_getresuid(struct registers *r) {
    uint32_t *p[3] = { (uint32_t *)(uintptr_t)r->ebx, (uint32_t *)(uintptr_t)r->ecx,
                       (uint32_t *)(uintptr_t)r->edx };
    uint32_t v[3] = { current->ruid, current->uid, current->uid };

    for (int i = 0; i < 3; i++)
        if (!user_w(p[i], 4)) return -EFAULT;
    for (int i = 0; i < 3; i++) *p[i] = v[i];
    return 0;
}

// setregid(rgid, egid): 71, setresgid(rgid, egid, sgid): 170. One group id per process.
static int set_gid(uint32_t rg, uint32_t eg, uint32_t sg) {
    if (current->uid != 0 &&
        (!may_become(rg, current->gid, current->gid) || !may_become(eg, current->gid, current->gid) ||
         !may_become(sg, current->gid, current->gid)))
        return -EPERM;
    if (eg != NOCHANGE)      current->gid = eg;
    else if (rg != NOCHANGE) current->gid = rg;
    vfs_set_creds(current->uid, current->gid);
    return 0;
}
static int sys_setregid(struct registers *r)   { return set_gid(r->ebx, r->ecx, NOCHANGE); }
static int sys_setresgid(struct registers *r)  { return set_gid(r->ebx, r->ecx, r->edx); }

// getresgid(&rgid, &egid, &sgid): 171
static int sys_getresgid(struct registers *r) {
    uint32_t *p[3] = { (uint32_t *)(uintptr_t)r->ebx, (uint32_t *)(uintptr_t)r->ecx,
                       (uint32_t *)(uintptr_t)r->edx };

    for (int i = 0; i < 3; i++)
        if (!user_w(p[i], 4)) return -EFAULT;
    for (int i = 0; i < 3; i++) *p[i] = current->gid;
    return 0;
}

// ---------- file size and attributes ---------- //

static int do_truncate(struct vfs_file *f, uint32_t len) {
    struct vfs_node *n = f->node;

    if (n->type == VFS_DIR) return -EISDIR;
    if (n->type != VFS_FILE || !n->ops || !n->ops->truncate) return -EINVAL;
    return n->ops->truncate(n, len);
}

// truncate(path, length): 92
static int sys_truncate(struct registers *r) {
    char path[VFS_PATH_MAX];
    int err;

    if ((int)r->ecx < 0) return -EINVAL;
    if ((err = syscall_user_path((const char *)(uintptr_t)r->ebx, path)) != 0) return err;

    struct vfs_file *f = vfs_file_open(path, O_WRONLY, &err);     // checks write permission
    if (!f) return err;
    err = do_truncate(f, r->ecx);
    vfs_file_put(f);
    return err;
}

// ftruncate(fd, length): 93
static int sys_ftruncate(struct registers *r) {
    int fd = (int)r->ebx;

    if (!fd_ok(fd)) return -EBADF;
    if ((int)r->ecx < 0) return -EINVAL;
    if ((current->fds[fd]->flags & O_ACCMODE) == O_RDONLY) return -EINVAL;
    return do_truncate(current->fds[fd], r->ecx);
}

// fchmod(fd, mode): 94
static int sys_fchmod(struct registers *r) {
    int fd = (int)r->ebx;

    if (!fd_ok(fd)) return -EBADF;
    struct vfs_node *n = current->fds[fd]->node;

    if (current->uid != 0 && current->uid != n->uid) return -EPERM;     // owner or root only
    if (!n->ops || !n->ops->setattr) return -EROFS;
    return n->ops->setattr(n, (int)(r->ecx & 07777), -1, -1);
}

// fchown(fd, uid, gid): 95
static int sys_fchown(struct registers *r) {
    int fd = (int)r->ebx;

    if (!fd_ok(fd)) return -EBADF;
    struct vfs_node *n = current->fds[fd]->node;

    if (current->uid != 0) return -EPERM;                               // root only, as chown
    if (!n->ops || !n->ops->setattr) return -EROFS;
    return n->ops->setattr(n, -1, (int)r->ecx, (int)r->edx);            // -1 = unchanged
}

// lchown(path, uid, gid): 198. Symbolic links are not told apart from their targets yet.
static int sys_lchown(struct registers *r) {
    char path[VFS_PATH_MAX];
    int err = syscall_user_path((const char *)(uintptr_t)r->ebx, path);

    if (err) return err;
    return vfs_chown(path, (int)r->ecx, (int)r->edx);
}

void sys_posix_init(void) {
    syscall_register(114, sys_wait4);
    syscall_register(190, sys_vfork);
    syscall_register(158, sys_sched_yield);
    syscall_register(147, sys_getsid);
    syscall_register(116, sys_sysinfo);
    syscall_register(43,  sys_times);
    syscall_register(77,  sys_getrusage);
    syscall_register(76,  sys_getrlimit);
    syscall_register(191, sys_getrlimit);
    syscall_register(75,  sys_setrlimit);
    syscall_register(34,  sys_nice);
    syscall_register(96,  sys_getpriority);
    syscall_register(97,  sys_setpriority);
    syscall_register(80,  sys_getgroups);
    syscall_register(81,  sys_setgroups);
    syscall_register(70,  sys_setreuid);
    syscall_register(71,  sys_setregid);
    syscall_register(164, sys_setresuid);
    syscall_register(165, sys_getresuid);
    syscall_register(170, sys_setresgid);
    syscall_register(171, sys_getresgid);
    syscall_register(92,  sys_truncate);
    syscall_register(93,  sys_ftruncate);
    syscall_register(94,  sys_fchmod);
    syscall_register(95,  sys_fchown);
    syscall_register(198, sys_lchown);
}