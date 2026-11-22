#include <stdint.h>
#include <errno.h>
#include "syscall.h"
#include "proc.h"
#include "vfs.h"

// setuid(uid): 23. root becomes uid for good; others can only drop back to their real user.
static int sys_setuid(struct registers *r) {
    uint32_t uid = r->ebx;

    if (current->uid == 0) {
        current->ruid = uid;
        current->uid  = uid;
    } else if (uid == current->ruid) {
        current->uid = uid;
    } else {
        return -EPERM;
    }
    vfs_set_creds(current->uid, current->gid);
    return 0;
}

// setgid(gid): 46
static int sys_setgid(struct registers *r) {
    uint32_t gid = r->ebx;

    if (current->uid != 0 && gid != current->gid) return -EPERM;
    current->gid = gid;
    vfs_set_creds(current->uid, current->gid);
    return 0;
}

static int sys_getuid(struct registers *r)  { (void)r; return (int)current->ruid; }   // 24
static int sys_geteuid(struct registers *r) { (void)r; return (int)current->uid; }    // 49
static int sys_getgid(struct registers *r)  { (void)r; return (int)current->gid; }    // 47
static int sys_getegid(struct registers *r) { (void)r; return (int)current->gid; }    // 50

// setpgid(pid, pgid): 57
static int sys_setpgid(struct registers *r) {
    return proc_setpgid((int)r->ebx, (int)r->ecx);
}

// setsid(): 66. There are no sessions here: it just puts the caller in a new process group
// of its own, which is what a pty needs (TIOCSCTTY makes that group the foreground one).
static int sys_setsid(struct registers *r) {
    (void)r;
    int err = proc_setpgid(0, 0);
    return err < 0 ? err : current->pid;
}

// getpgrp(): 65
static int sys_getpgrp(struct registers *r) {
    (void)r;
    return current->pgid;
}

// getpgid(pid): 132
static int sys_getpgid(struct registers *r) {
    return proc_getpgid((int)r->ebx);
}

void sys_cred_init(void) {
    syscall_register(23,  sys_setuid);
    syscall_register(24,  sys_getuid);
    syscall_register(46,  sys_setgid);
    syscall_register(47,  sys_getgid);
    syscall_register(49,  sys_geteuid);
    syscall_register(50,  sys_getegid);
    syscall_register(57,  sys_setpgid);
    syscall_register(65,  sys_getpgrp);
    syscall_register(66,  sys_setsid);
    syscall_register(132, sys_getpgid);
}