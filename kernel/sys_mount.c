#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "syscall.h"
#include "mount.h"
#include "blkdev.h"
#include "ext2.h"
#include "vfs.h"
#include "proc.h"
#include "kprintf.h"

#define MTAB_MAX 16

// The block devices behind the mounts (the VFS only knows paths)
struct mtent {
    int      used;
    char     target[VFS_PATH_MAX];
    int      disk;
    uint32_t start, sectors;
};

static struct mtent mtab[MTAB_MAX];

static struct mtent *mtab_slot(void) {
    for (int i = 0; i < MTAB_MAX; i++)
        if (!mtab[i].used) return &mtab[i];
    return NULL;
}

static struct mtent *mtab_find(const char *target) {
    for (int i = 0; i < MTAB_MAX; i++)
        if (mtab[i].used && strcmp(mtab[i].target, target) == 0) return &mtab[i];
    return NULL;
}

void mount_record(const char *target, int disk, uint32_t start, uint32_t sectors) {
    struct mtent *m = mtab_slot();
    if (!m || strlen(target) >= VFS_PATH_MAX) return;

    m->used = 1;
    memcpy(m->target, target, strlen(target) + 1);
    m->disk    = disk;
    m->start   = start;
    m->sectors = sectors;
}

int mount_device(const char *target, char *dev, size_t size) {
    struct mtent *m = mtab_find(target);
    if (!m) return -1;
    return blkdev_name(m->disk, m->start, dev, (uint32_t)size);
}

// mount(source, target, fstype, flags, data): 21
static int sys_mount(struct registers *r) {
    char src[VFS_PATH_MAX], tgt[VFS_PATH_MAX], norm[VFS_PATH_MAX], fst[16];
    struct vfs_stat st;
    int disk, err;
    uint32_t start, sectors;

    if (current->uid != 0) return -EPERM;
    // Restrictions we cannot enforce (nosuid, nodev, noexec, sync, noatime, relatime, silent)
    // are accepted and ignored. Read-only, remount, bind and move are refused.
    #define MS_IGNORED (0x1Eu | 0xC00u | 0x8000u | 0x200000u | 0x1000000u)
    uint32_t flags = r->esi;
    if ((flags & 0xFFFF0000u) == 0xC0ED0000u) flags &= 0xFFFFu;      // the old magic in the top half
    if (flags & ~MS_IGNORED) return -EINVAL;

    if ((err = syscall_user_path((const char *)(uintptr_t)r->ebx, src)) != 0) return err;
    if ((err = syscall_user_path((const char *)(uintptr_t)r->ecx, tgt)) != 0) return err;

    fst[0] = '\0';
    if (r->edx && copy_str_from_user(fst, (const char *)(uintptr_t)r->edx, sizeof(fst)) < 0)
        return -EINVAL;
    if (fst[0] && strcmp(fst, "ext2") != 0 && strcmp(fst, "auto") != 0) return -ENODEV;

    if ((err = vfs_normalize(tgt, norm)) != 0) return err;

    // the source must be one of our block devices
    struct vfs_file *f = vfs_file_open(src, O_RDONLY, &err);
    if (!f) return err;
    int isblk = blkdev_from_node(f->node, &disk, &start, &sectors);
    vfs_file_put(f);
    if (isblk != 0) return -EINVAL;                         // not a block device

    // the target must be an existing directory
    if ((err = vfs_stat(norm, &st)) != 0) return err;
    if (st.type != VFS_DIR) return -ENOTDIR;

    // never mount the same blocks twice: two ext2 instances would corrupt each other
    for (int i = 0; i < MTAB_MAX; i++) {
        if (!mtab[i].used || mtab[i].disk != disk) continue;
        if (start < mtab[i].start + mtab[i].sectors && mtab[i].start < start + sectors)
            return -EBUSY;
    }

    struct mtent *m = mtab_slot();
    if (!m) return -ENOSPC;

    struct vfs_node *root = ext2_mount(disk, start);
    if (!root) return -EINVAL;                              // not an ext2 filesystem

    err = vfs_mount(norm, root);
    if (err) {
        if (root->ops && root->ops->unmount) root->ops->unmount(root);
        return err;
    }

    m->used = 1;
    memcpy(m->target, norm, strlen(norm) + 1);
    m->disk    = disk;
    m->start   = start;
    m->sectors = sectors;
    return 0;
}

// umount(target): 22
static int sys_umount(struct registers *r) {
    char path[VFS_PATH_MAX], norm[VFS_PATH_MAX];
    int err;

    if (current->uid != 0) return -EPERM;
    if ((err = syscall_user_path((const char *)(uintptr_t)r->ebx, path)) != 0) return err;
    if ((err = vfs_normalize(path, norm)) != 0) return err;

    if (norm[1] != '\0' && proc_cwd_inside(norm)) return -EBUSY;   // somebody is standing in it

    if ((err = vfs_umount(norm)) != 0) return err;

    struct mtent *m = mtab_find(norm);
    if (m) m->used = 0;
    return 0;
}

// umount2(target, flags): 52. MNT_FORCE is accepted and changes nothing; lazy unmounts are not supported
static int sys_umount2(struct registers *r) {
    if (r->ecx & ~1u) return -EINVAL;
    return sys_umount(r);
}

void sys_mount_init(void) {
    syscall_register(21, sys_mount);
    syscall_register(22, sys_umount);
    syscall_register(52, sys_umount2);
}