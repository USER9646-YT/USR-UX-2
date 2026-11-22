#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "syscall.h"
#include "proc.h"
#include "vfs.h"
#include "paging.h"

static void fill_stat(struct stat *u, const struct vfs_stat *vs) {
    u->st_ino  = vs->ino;
    u->st_mode = vs->mode;
    u->st_uid  = vs->uid;
    u->st_gid  = vs->gid;
    u->st_size = vs->size;
}

// fstat(fd, st): 108
static int sys_fstat(struct registers *r) {
    int fd = (int)r->ebx;
    struct stat *u = (struct stat *)(uintptr_t)r->ecx;
    struct vfs_file *f = (fd >= 0 && fd < VFS_MAX_FDS) ? current->fds[fd] : NULL;

    if (!f) return -EBADF;
    if (!paging_check_user((uint32_t)(uintptr_t)u, sizeof(*u), 1)) return -EFAULT;

    struct vfs_stat vs = {
        .ino = f->node->ino, .type = f->node->type, .size = f->node->size,
        .mode = f->node->mode, .uid = f->node->uid, .gid = f->node->gid,
    };
    fill_stat(u, &vs);
    return 0;
}

// lstat(path, st): 107. There are no symbolic links to follow yet, so it is stat.
static int sys_lstat(struct registers *r) {
    char path[VFS_PATH_MAX];
    struct stat *u = (struct stat *)(uintptr_t)r->ecx;
    struct vfs_stat vs;

    int err = syscall_user_path((const char *)(uintptr_t)r->ebx, path);
    if (err) return err;
    if (!paging_check_user((uint32_t)(uintptr_t)u, sizeof(*u), 1)) return -EFAULT;

    err = vfs_stat(path, &vs);
    if (err) return err;
    fill_stat(u, &vs);
    return 0;
}

// access(path, mode): 33. mode: 0 = exists, 4 = read, 2 = write, 1 = execute. Uses the effective user.
static int sys_access(struct registers *r) {
    char path[VFS_PATH_MAX];

    int err = syscall_user_path((const char *)(uintptr_t)r->ebx, path);
    if (err) return err;
    if (r->ecx & ~7u) return -EINVAL;
    return vfs_access_path(path, (int)r->ecx);
}

// umask(mask): 60. Remembered, but not applied to new files yet.
static int sys_umask(struct registers *r) {
    static int mask = 022;
    int old = mask;

    mask = (int)(r->ebx & 0777);
    return old;
}

void sys_misc_init(void) {
    syscall_register(108, sys_fstat);
    syscall_register(107, sys_lstat);
    syscall_register(33,  sys_access);
    syscall_register(60,  sys_umask);
}