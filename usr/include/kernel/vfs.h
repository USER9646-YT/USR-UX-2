#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX   255
#define VFS_PATH_MAX   512
#define VFS_MAX_FDS    32
#define VFS_MAX_MOUNTS 8

// permission bits for vfs_access
#define ACC_R 4
#define ACC_W 2
#define ACC_X 1

enum vfs_type { VFS_FILE = 1, VFS_DIR = 2, VFS_SYMLINK = 3, VFS_OTHER = 4, VFS_CHAR = 5 };

struct vfs_node;

struct vfs_dirent {
    char     name[VFS_NAME_MAX + 1];
    uint32_t ino;
    uint32_t type;
};

struct vfs_stat {
    uint32_t ino;
    uint32_t type;
    uint32_t size;
    uint32_t mode;      // POSIX: file type bits + permission bits
    uint32_t uid;
    uint32_t gid;
};

struct vfs_ops {
    int (*read)(struct vfs_node *n, uint32_t off, uint32_t len, void *buf);
    int (*write)(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf);
    int (*readdir)(struct vfs_node *dir, uint32_t index, struct vfs_dirent *out);
    struct vfs_node *(*finddir)(struct vfs_node *dir, const char *name);
    int (*create)(struct vfs_node *dir, const char *name, uint32_t type,
                  struct vfs_node **out);
    void (*release)(struct vfs_node *n);
    // change mode/uid/gid; -1 means "leave unchanged". The fs updates the node too.
    int (*setattr)(struct vfs_node *n, int mode, int uid, int gid);
    // remove a non-directory entry / an empty directory
    int (*unlink)(struct vfs_node *dir, const char *name);
    int (*rmdir)(struct vfs_node *dir, const char *name);
    // move an entry; both directories belong to the same filesystem type
    int (*rename)(struct vfs_node *olddir, const char *oldname,
                  struct vfs_node *newdir, const char *newname);
    int (*truncate)(struct vfs_node *n, uint32_t size);
    // device control; arg may be a user-space pointer, so the driver must validate it
    int (*ioctl)(struct vfs_node *n, uint32_t req, uint32_t arg);
    // maps device memory into user space: *phys = physical address of byte `offset`
    // (page aligned), valid for `len` bytes */
    int (*mmap)(struct vfs_node *n, uint32_t offset, uint32_t len, uint32_t *phys);
    // which of POLLIN / POLLOUT / POLLERR / POLLHUP would not block right now.
    // A node without this operation is always ready (regular files, /dev/null, ...).
    int (*poll)(struct vfs_node *n);
    // called when the node is opened. May replace the node: set *out to a new node
    // (one reference, which the open file takes over). /dev/ptmx uses this to hand out
    // a fresh pty master. Return 0 or -errno.
    int (*open)(struct vfs_node *n, struct vfs_node **out);
};

struct vfs_node {
    uint32_t        ino;
    uint32_t        type;
    uint32_t        size;
    uint32_t        refs;
    uint16_t        mode;
    uint16_t        uid;
    uint16_t        gid;
    struct vfs_ops *ops;
    void           *priv;
};

// An open file: shared by every descriptor that was dup'ed or inherited via fork
struct vfs_file {
    struct vfs_node *node;
    uint32_t         offset;     // byte offset, or entry index for directories
    int              flags;
    int              refs;
};

// identity of the process the kernel is currently working for (set on every context switch)
extern uint32_t vfs_uid, vfs_gid;
void vfs_set_creds(uint32_t uid, uint32_t gid);

// 1 while a read/write on an O_NONBLOCK descriptor is in progress: a device that
// would have to wait returns -EAGAIN instead (it must look at this before sleeping)
extern int vfs_nonblock;

void vfs_init(void);
void vfs_node_put(struct vfs_node *n);
int  vfs_mount(const char *path, struct vfs_node *root);   // takes over the caller's reference
int  vfs_normalize(const char *path, char *out);           // out: VFS_PATH_MAX bytes

/* permissions */
int  vfs_access(const struct vfs_node *n, uint32_t uid, uint32_t gid, int want);  // 0 or -EACCES
int  vfs_access_path(const char *path, int want);
int  vfs_chmod(const char *path, uint32_t mode);
int  vfs_chown(const char *path, int uid, int gid);

// open file objects
struct vfs_file *vfs_file_alloc(struct vfs_node *n, int flags);
struct vfs_file *vfs_file_open(const char *path, int flags, int *err);
void    vfs_file_ref(struct vfs_file *f);
void    vfs_file_put(struct vfs_file *f);
int     vfs_file_read(struct vfs_file *f, void *buf, uint32_t len);
int     vfs_file_write(struct vfs_file *f, const void *buf, uint32_t len);
int32_t vfs_file_lseek(struct vfs_file *f, int32_t off, int whence);
int     vfs_file_readdir(struct vfs_file *f, struct vfs_dirent *out);

// kernel-internal descriptors (used by the ELF loader and boot code)
int     vfs_open(const char *path, int flags);
int     vfs_close(int fd);
int     vfs_read(int fd, void *buf, uint32_t len);
int     vfs_write(int fd, const void *buf, uint32_t len);
int32_t vfs_lseek(int fd, int32_t off, int whence);
int     vfs_readdir(int fd, struct vfs_dirent *out);

int     vfs_stat(const char *path, struct vfs_stat *st);
int     vfs_mkdir(const char *path);
int     vfs_unlink(const char *path);
int     vfs_rmdir(const char *path);
int     vfs_rename(const char *oldpath, const char *newpath);

#endif