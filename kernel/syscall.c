#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/procinfo.h>
#include <sys/reboot.h>
#include <dirent.h>
#include "syscall.h"
#include "idt.h"
#include "paging.h"
#include "pmm.h"
#include "vfs.h"
#include "proc.h"
#include "pipe.h"
#include "pit.h"
#include "power.h"
#include "hostname.h"

#define OS_SYSNAME  "USR/UX"
#define OS_RELEASE  "2 0.95b"
#define OS_VERSION  "#1 Wed 25 Nov 2026 13:29:28 +0100"
#define OS_MACHINE  "i386"

#define SYS_EXIT      1
#define SYS_FORK      2
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_WAITPID   7
#define SYS_UNLINK   10
#define SYS_EXECVE   11
#define SYS_CHDIR    12
#define SYS_CHMOD    15
#define SYS_CHOWN    16
#define SYS_LSEEK    19
#define SYS_GETPID   20
#define SYS_SYNC     36
#define SYS_KILL     37
#define SYS_RENAME   38
#define SYS_MKDIR    39
#define SYS_RMDIR    40
#define SYS_DUP      41
#define SYS_PIPE     42
#define SYS_DUP2     63
#define SYS_GETPPID  64
#define SYS_REBOOT   88
#define SYS_READDIR  89
#define SYS_MMAP_OLD 90          // takes a pointer to a struct of 6 arguments
#define SYS_MUNMAP   91
#define SYS_STAT    106
#define SYS_UNAME   122
#define MAX_EXTRA 160
#define SYS_NANOSLEEP 162
#define SYS_MMAP2   192          // 6 arguments in ebx, ecx, edx, esi, edi, ebp; offset in pages
#define SYS_GETCWD  183
#define SYS_PROCINFO 400

// memory mappings live between the heap and the stack
#define MMAP_BASE      0x60000000u
#define MMAP_END       0x7F000000u
#define MMAP_MAX_PAGES 16384u            // 64 MiB per mapping

#define PROT_WRITE     0x2
#define MAP_FIXED      0x10
#define MAP_ANONYMOUS  0x20
#define PTE_NOCACHE    0x18              // PWT | PCD, for device memory

int vfs_nonblock;                        // see vfs.h 

// syscalls implemented in other files (kernel/sys_*.c)
#define MAX_EXTRA 48
static struct { uint32_t num; syscall_fn fn; } extra[MAX_EXTRA];
static int nextra;

int syscall_register(uint32_t num, syscall_fn fn) {
    if (nextra >= MAX_EXTRA) return -1;
    extra[nextra].num = num;
    extra[nextra].fn  = fn;
    nextra++;
    return 0;
}

static struct vfs_file *get_fd(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return NULL;
    return current->fds[fd];
}

static int install_fd(struct vfs_file *f) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!current->fds[i]) {
            current->fds[i] = f;
            return i;
        }
    }
    return -EMFILE;
}

// Copies a path from user space and makes it absolute using the process's cwd
int syscall_user_path(const char *upath, char *out) {
    char tmp[VFS_PATH_MAX];
    int n = copy_str_from_user(tmp, upath, sizeof(tmp));
    if (n < 0) return n;
    if (n == 0) return -ENOENT;

    if (tmp[0] == '/') {
        memcpy(out, tmp, (size_t)n + 1);
        return 0;
    }

    size_t cl = strlen(current->cwd);
    if (cl + 1 + (size_t)n >= VFS_PATH_MAX) return -ENAMETOOLONG;

    memcpy(out, current->cwd, cl);
    out[cl] = '/';
    memcpy(out + cl + 1, tmp, (size_t)n + 1);
    return 0;                       // vfs normalizes "//", "." and ".."
}

static int sys_write(int fd, const char *buf, uint32_t len) {
    struct vfs_file *f = get_fd(fd);
    if (!f) return -EBADF;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;                         // short writes are legal
    if (!paging_check_user((uint32_t)(uintptr_t)buf, len, 0)) return -EFAULT;

    vfs_nonblock = (f->flags & O_NONBLOCK) != 0;
    int r = vfs_file_write(f, buf, len);
    vfs_nonblock = 0;
    return r;
}

static int sys_read(int fd, char *buf, uint32_t len) {
    struct vfs_file *f = get_fd(fd);
    if (!f) return -EBADF;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;
    if (!paging_check_user((uint32_t)(uintptr_t)buf, len, 1)) return -EFAULT;

    vfs_nonblock = (f->flags & O_NONBLOCK) != 0;
    int r = vfs_file_read(f, buf, len);
    vfs_nonblock = 0;
    return r;
}

static int sys_open(const char *upath, int flags) {
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    if (r) return r;

    int err = 0;
    struct vfs_file *f = vfs_file_open(path, flags, &err);
    if (!f) return err;

    int fd = install_fd(f);
    if (fd < 0) vfs_file_put(f);
    return fd;
}

static int sys_close(int fd) {
    struct vfs_file *f = get_fd(fd);
    if (!f) return -EBADF;

    current->fds[fd] = NULL;
    vfs_file_put(f);
    return 0;
}

static int sys_dup(int fd) {
    struct vfs_file *f = get_fd(fd);
    if (!f) return -EBADF;

    vfs_file_ref(f);
    int n = install_fd(f);
    if (n < 0) vfs_file_put(f);
    return n;
}

static int sys_dup2(int oldfd, int newfd) {
    struct vfs_file *f = get_fd(oldfd);
    if (!f) return -EBADF;
    if (newfd < 0 || newfd >= VFS_MAX_FDS) return -EBADF;
    if (oldfd == newfd) return newfd;

    if (current->fds[newfd]) vfs_file_put(current->fds[newfd]);     // close what was there
    vfs_file_ref(f);
    current->fds[newfd] = f;
    return newfd;
}

static int sys_pipe(int *ufds) {
    if (!paging_check_user((uint32_t)(uintptr_t)ufds, 2 * sizeof(int), 1)) return -EFAULT;

    struct vfs_file *rd, *wr;
    int r = pipe_create(&rd, &wr);
    if (r) return r;

    int fd0 = install_fd(rd);
    if (fd0 < 0) {
        vfs_file_put(rd);
        vfs_file_put(wr);
        return fd0;
    }

    int fd1 = install_fd(wr);
    if (fd1 < 0) {
        current->fds[fd0] = NULL;
        vfs_file_put(rd);
        vfs_file_put(wr);
        return fd1;
    }

    ufds[0] = fd0;
    ufds[1] = fd1;
    return 0;
}

static int sys_lseek(int fd, int off, int whence) {
    struct vfs_file *f = get_fd(fd);
    if (!f) return -EBADF;
    return vfs_file_lseek(f, off, whence);
}

static int sys_stat(const char *upath, struct stat *ust) {
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    if (r) return r;
    if (!paging_check_user((uint32_t)(uintptr_t)ust, sizeof(*ust), 1)) return -EFAULT;

    struct vfs_stat vs;
    r = vfs_stat(path, &vs);
    if (r) return r;

    ust->st_ino  = vs.ino;
    ust->st_mode = vs.mode;
    ust->st_uid  = vs.uid;
    ust->st_gid  = vs.gid;
    ust->st_size = vs.size;
    return 0;
}

static int sys_readdir(int fd, struct dirent *ud) {
    struct vfs_file *f = get_fd(fd);
    if (!f) return -EBADF;
    if (!paging_check_user((uint32_t)(uintptr_t)ud, sizeof(*ud), 1)) return -EFAULT;

    struct vfs_dirent de;
    int r = vfs_file_readdir(f, &de);
    if (r <= 0) return r;

    ud->d_ino  = de.ino;
    ud->d_type = de.type;
    memcpy(ud->d_name, de.name, strlen(de.name) + 1);
    return 1;
}

static int sys_chdir(const char *upath) {
    char path[VFS_PATH_MAX], norm[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    if (r) return r;

    r = vfs_normalize(path, norm);
    if (r) return r;

    struct vfs_stat vs;
    r = vfs_stat(norm, &vs);
    if (r) return r;
    if (vs.type != VFS_DIR) return -ENOTDIR;

    memcpy(current->cwd, norm, strlen(norm) + 1);
    return 0;
}

static int sys_getcwd(char *buf, uint32_t size) {
    uint32_t len = (uint32_t)strlen(current->cwd) + 1;
    if (size < len) return -ERANGE;
    if (!paging_check_user((uint32_t)(uintptr_t)buf, len, 1)) return -EFAULT;

    memcpy(buf, current->cwd, len);
    return (int)len;
}

static int sys_mkdir(const char *upath) {            // the mode argument is ignored for now
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    return r ? r : vfs_mkdir(path);
}

static int sys_rmdir(const char *upath) {
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    return r ? r : vfs_rmdir(path);
}

static int sys_unlink(const char *upath) {
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    return r ? r : vfs_unlink(path);
}

static int sys_rename(const char *uold, const char *unew) {
    char oldp[VFS_PATH_MAX], newp[VFS_PATH_MAX];
    int r = syscall_user_path(uold, oldp);
    if (r) return r;
    r = syscall_user_path(unew, newp);
    if (r) return r;
    return vfs_rename(oldp, newp);
}

static void fill_field(char *dst, const char *src) {
    memcpy(dst, src, strlen(src) + 1);              // every field has 65 bytes
}

static int sys_uname(struct utsname *u) {
    if (!paging_check_user((uint32_t)(uintptr_t)u, sizeof(*u), 1)) return -EFAULT;

    memset(u, 0, sizeof(*u));
    fill_field(u->sysname,  OS_SYSNAME);
    fill_field(u->nodename, hostname_get());
    fill_field(u->release,  OS_RELEASE);
    fill_field(u->version,  OS_VERSION);
    fill_field(u->machine,  OS_MACHINE);
    return 0;
}

static int sys_waitpid(int pid, int *ustatus, int options) {
    if (ustatus && !paging_check_user((uint32_t)(uintptr_t)ustatus, sizeof(int), 1))
        return -EFAULT;

    int status = 0;
    int r = proc_waitpid(pid, &status, options);
    if (r > 0 && ustatus) *ustatus = status;
    return r;
}

static int sys_chmod(const char *upath, uint32_t mode) {
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    return r ? r : vfs_chmod(path, mode);
}

static int sys_chown(const char *upath, int uid, int gid) {
    char path[VFS_PATH_MAX];
    int r = syscall_user_path(upath, path);
    return r ? r : vfs_chown(path, uid, gid);
}

// ---------- memory mappings ---------- //

// Frees the pages of a mapping. Device memory lies outside RAM, which pmm_free ignores.
static void unmap_range(uint32_t addr, uint32_t pages) {
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t a = addr + i * PAGE_SIZE;
        uint32_t phys = paging_get_phys(a) & ~(PAGE_SIZE - 1);

        if (!phys) continue;
        paging_unmap(a);
        pmm_free(phys);
    }
}

// First gap of `pages` unmapped pages in the mapping area
static int find_range(uint32_t pages, uint32_t *out) {
    uint32_t run = 0, start = 0;

    for (uint32_t a = MMAP_BASE; a < MMAP_END; a += PAGE_SIZE) {
        if (paging_get_phys(a)) { run = 0; continue; }
        if (run == 0) start = a;
        if (++run == pages) { *out = start; return 0; }
    }
    return -ENOMEM;
}

static int sys_mmap(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags,
                    int fd, uint32_t pgoff) {
    struct vfs_file *f = NULL;
    uint32_t base, i;

    if (len == 0) return -EINVAL;
    uint32_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > MMAP_MAX_PAGES) return -ENOMEM;

    if (!(flags & MAP_ANONYMOUS)) {
        f = get_fd(fd);
        if (!f) return -EBADF;
        if (!f->node->ops || !f->node->ops->mmap) return -ENODEV;
    }

    if (flags & MAP_FIXED) {
        if ((addr & (PAGE_SIZE - 1)) || addr < MMAP_BASE || addr >= MMAP_END ||
            pages > (MMAP_END - addr) / PAGE_SIZE)
            return -EINVAL;
        unmap_range(addr, pages);
        base = addr;
    } else {
        int r = find_range(pages, &base);
        if (r) return r;
    }

    uint32_t pte = PTE_USER | ((prot & PROT_WRITE) ? PTE_WRITE : 0);

    if (!f) {                                       // anonymous memory: fresh zeroed frames
        for (i = 0; i < pages; i++) {
            uint32_t frame = pmm_alloc();
            if (!frame) { unmap_range(base, i); return -ENOMEM; }
            memset((void *)frame, 0, PAGE_SIZE);
            if (paging_map(base + i * PAGE_SIZE, frame, pte) != 0) {
                pmm_free(frame);
                unmap_range(base, i);
                return -ENOMEM;
            }
        }
        return (int)base;
    }

    uint32_t phys;                                  // a device: map its memory, uncached
    int r = f->node->ops->mmap(f->node, pgoff * PAGE_SIZE, pages * PAGE_SIZE, &phys);
    if (r) return r;

    for (i = 0; i < pages; i++) {
        if (paging_map(base + i * PAGE_SIZE, phys + i * PAGE_SIZE, pte | PTE_NOCACHE) != 0) {
            unmap_range(base, i);
            return -ENOMEM;
        }
    }
    return (int)base;
}

struct old_mmap_args {
    uint32_t addr, len, prot, flags, fd, offset;
};

static int sys_old_mmap(const struct old_mmap_args *u) {
    struct old_mmap_args a;

    if (!paging_check_user((uint32_t)(uintptr_t)u, sizeof(a), 0)) return -EFAULT;
    memcpy(&a, u, sizeof(a));
    if (a.offset & (PAGE_SIZE - 1)) return -EINVAL;
    return sys_mmap(a.addr, a.len, a.prot, a.flags, (int)a.fd, a.offset / PAGE_SIZE);
}

static int sys_munmap(uint32_t addr, uint32_t len) {
    if ((addr & (PAGE_SIZE - 1)) || len == 0) return -EINVAL;

    uint32_t pages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
    if (addr < MMAP_BASE || addr >= MMAP_END || pages > (MMAP_END - addr) / PAGE_SIZE)
        return -EINVAL;

    unmap_range(addr, pages);
    return 0;
}

// ---------- time, power, processes ---------- //

static int sys_nanosleep(const struct timespec *ureq) {
    struct timespec req;

    if (!paging_check_user((uint32_t)(uintptr_t)ureq, sizeof(req), 0)) return -EFAULT;
    memcpy(&req, ureq, sizeof(req));
    if (req.tv_sec < 0 || req.tv_nsec < 0 || req.tv_nsec >= 1000000000) return -EINVAL;

    uint32_t hz  = pit_freq();
    uint32_t sec = (uint32_t)req.tv_sec;
    if (sec > 1000000u) sec = 1000000u;
    uint32_t ms    = (uint32_t)req.tv_nsec / 1000000u;
    uint32_t ticks = sec * hz + (ms * hz + 999u) / 1000u;
    if (ticks == 0 && (req.tv_sec || req.tv_nsec)) ticks = 1;

    return proc_sleep(ticks);
}

static int sys_reboot(uint32_t cmd) {
    if (current->uid != 0) return -EPERM;

    switch (cmd) {
    case RB_AUTOBOOT: power_reboot();
    case RB_HALT:     power_halt();
    case RB_POWEROFF: power_off();
    default:          return -EINVAL;
    }
}

static int sys_procinfo(int index, struct procinfo *u) {
    if (!paging_check_user((uint32_t)(uintptr_t)u, sizeof(*u), 1)) return -EFAULT;

    struct procinfo pi;
    int r = proc_info(index, &pi);
    if (r > 0) memcpy(u, &pi, sizeof(pi));
    return r;
}

static void syscall_handler(struct registers *r) {
    sys_tty_hook();                                       // e.g. restore the console if a graphics program died

    // registered syscalls first: they can override the built-in ones
    for (int i = 0; i < nextra; i++) {
        if (extra[i].num == r->eax) {
            r->eax = (uint32_t)extra[i].fn(r);
            goto done;
        }
    }

    switch (r->eax) {
    case SYS_EXIT:
        proc_exit((int)r->ebx);                           // does not return
    case SYS_FORK:
        r->eax = (uint32_t)proc_fork(r);
        break;
    case SYS_READ:
        r->eax = (uint32_t)sys_read((int)r->ebx, (char *)(uintptr_t)r->ecx, r->edx);
        break;
    case SYS_WRITE:
        r->eax = (uint32_t)sys_write((int)r->ebx, (const char *)(uintptr_t)r->ecx, r->edx);
        break;
    case SYS_OPEN:
        r->eax = (uint32_t)sys_open((const char *)(uintptr_t)r->ebx, (int)r->ecx);
        break;
    case SYS_CLOSE:
        r->eax = (uint32_t)sys_close((int)r->ebx);
        break;
    case SYS_WAITPID:
        r->eax = (uint32_t)sys_waitpid((int)r->ebx, (int *)(uintptr_t)r->ecx, (int)r->edx);
        break;
    case SYS_UNLINK:
        r->eax = (uint32_t)sys_unlink((const char *)(uintptr_t)r->ebx);
        break;
    case SYS_EXECVE:
        r->eax = (uint32_t)proc_exec(r, (const char *)(uintptr_t)r->ebx,
                                     (char *const *)(uintptr_t)r->ecx,
                                     (char *const *)(uintptr_t)r->edx);
        break;
    case SYS_CHDIR:
        r->eax = (uint32_t)sys_chdir((const char *)(uintptr_t)r->ebx);
        break;
    case SYS_CHMOD:
        r->eax = (uint32_t)sys_chmod((const char *)(uintptr_t)r->ebx, r->ecx);
        break;
    case SYS_CHOWN:
        r->eax = (uint32_t)sys_chown((const char *)(uintptr_t)r->ebx, (int)r->ecx, (int)r->edx);
        break;
    case SYS_LSEEK:
        r->eax = (uint32_t)sys_lseek((int)r->ebx, (int)r->ecx, (int)r->edx);
        break;
    case SYS_GETPID:
        r->eax = (uint32_t)current->pid;
        break;
    case SYS_GETPPID:
        r->eax = (uint32_t)current->ppid;
        break;
    case SYS_SYNC:
        r->eax = 0;                                       // every write already goes straight to disk
        break;
    case SYS_KILL:
        r->eax = (uint32_t)proc_signal((int)r->ebx, (int)r->ecx);
        break;
    case SYS_RENAME:
        r->eax = (uint32_t)sys_rename((const char *)(uintptr_t)r->ebx,
                                      (const char *)(uintptr_t)r->ecx);
        break;
    case SYS_MKDIR:
        r->eax = (uint32_t)sys_mkdir((const char *)(uintptr_t)r->ebx);
        break;
    case SYS_RMDIR:
        r->eax = (uint32_t)sys_rmdir((const char *)(uintptr_t)r->ebx);
        break;
    case SYS_DUP:
        r->eax = (uint32_t)sys_dup((int)r->ebx);
        break;
    case SYS_PIPE:
        r->eax = (uint32_t)sys_pipe((int *)(uintptr_t)r->ebx);
        break;
    case SYS_DUP2:
        r->eax = (uint32_t)sys_dup2((int)r->ebx, (int)r->ecx);
        break;
    case SYS_REBOOT:
        r->eax = (uint32_t)sys_reboot(r->ebx);
        break;
    case SYS_READDIR:
        r->eax = (uint32_t)sys_readdir((int)r->ebx, (struct dirent *)(uintptr_t)r->ecx);
        break;
    case SYS_MMAP_OLD:
        r->eax = (uint32_t)sys_old_mmap((const struct old_mmap_args *)(uintptr_t)r->ebx);
        break;
    case SYS_MMAP2:
        r->eax = (uint32_t)sys_mmap(r->ebx, r->ecx, r->edx, r->esi, (int)r->edi, r->ebp);
        break;
    case SYS_MUNMAP:
        r->eax = (uint32_t)sys_munmap(r->ebx, r->ecx);
        break;
    case SYS_STAT:
        r->eax = (uint32_t)sys_stat((const char *)(uintptr_t)r->ebx,
                                    (struct stat *)(uintptr_t)r->ecx);
        break;
    case SYS_UNAME:
        r->eax = (uint32_t)sys_uname((struct utsname *)(uintptr_t)r->ebx);
        break;
    case SYS_NANOSLEEP:
        r->eax = (uint32_t)sys_nanosleep((const struct timespec *)(uintptr_t)r->ebx);
        break;
    case SYS_GETCWD:
        r->eax = (uint32_t)sys_getcwd((char *)(uintptr_t)r->ebx, r->ecx);
        break;
    case SYS_PROCINFO:
        r->eax = (uint32_t)sys_procinfo((int)r->ebx, (struct procinfo *)(uintptr_t)r->ecx);
        break;
    default:
        r->eax = (uint32_t)-ENOSYS;
        break;
    }

done:
    // a signal arrived while we were in the kernel: die before returning to user mode
    if (current->killed) proc_exit(128 + current->killed);
}

void syscall_init(void) {
    isr_register_handler(128, syscall_handler);
    syscall_register_all();
}