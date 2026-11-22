#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "syscall.h"
#include "proc.h"
#include "vfs.h"
#include "paging.h"
#include "hostname.h"

// sethostname(name, len): 74
static int sys_sethostname(struct registers *r) {
    const char *u = (const char *)(uintptr_t)r->ebx;
    uint32_t len = r->ecx;
    char buf[65];

    if (current->uid != 0) return -EPERM;
    if (len == 0 || len > 64) return -EINVAL;
    if (!paging_check_user((uint32_t)(uintptr_t)u, len, 0)) return -EFAULT;

    memcpy(buf, u, len);
    return hostname_set(buf, len);
}

// fcntl(fd, cmd, arg): 55
static int sys_fcntl(struct registers *r) {
    int fd = (int)r->ebx;
    int cmd = (int)r->ecx;
    int arg = (int)r->edx;
    struct vfs_file *f = (fd >= 0 && fd < VFS_MAX_FDS) ? current->fds[fd] : NULL;

    if (!f) return -EBADF;

    switch (cmd) {
    case F_DUPFD:
        if (arg < 0) return -EINVAL;
        for (int i = arg; i < VFS_MAX_FDS; i++) {
            if (!current->fds[i]) {
                vfs_file_ref(f);
                current->fds[i] = f;
                return i;
            }
        }
        return -EMFILE;
    case F_GETFD:
    case F_SETFD:
        return 0;                                   // close-on-exec is not implemented
    case F_GETFL:
        return f->flags;
    case F_SETFL:
        f->flags = (f->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
        return 0;
    default:
        return -EINVAL;
    }
}

void sys_extra_init(void) {
    syscall_register(74, sys_sethostname);
    syscall_register(55, sys_fcntl);
}