#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "vfs.h"
#include "kheap.h"
#include "kprintf.h"

struct vfs_mount {
    char             path[VFS_PATH_MAX];
    struct vfs_node *root;
};

static struct vfs_file  *fds[VFS_MAX_FDS];       // kernel-internal descriptors
static struct vfs_mount  mounts[VFS_MAX_MOUNTS];
static int               nmounts;

uint32_t vfs_uid;       // both start as 0: the kernel itself acts as root
uint32_t vfs_gid;

void vfs_init(void) {
    memset(fds, 0, sizeof(fds));
    memset(mounts, 0, sizeof(mounts));
    nmounts = 0;
}

void vfs_set_creds(uint32_t uid, uint32_t gid) {
    vfs_uid = uid;
    vfs_gid = gid;
}

void vfs_node_put(struct vfs_node *n) {
    if (!n) return;
    if (n->refs == 0) {
        kprintf("vfs: node %u released too often!\n", n->ino);
        return;
    }
    if (--n->refs == 0 && n->ops && n->ops->release)
        n->ops->release(n);
}

// ---------- permissions ---------- //

int vfs_access(const struct vfs_node *n, uint32_t uid, uint32_t gid, int want) {
    uint32_t mode = n->mode;
    uint32_t bits;

    if (uid == 0) {
        // root may read and write anything, but only run files that have some x bit
        if ((want & ACC_X) && n->type != VFS_DIR && !(mode & 0111)) return -EACCES;
        return 0;
    }

    if (uid == n->uid)       bits = (mode >> 6) & 7;
    else if (gid == n->gid)  bits = (mode >> 3) & 7;
    else                     bits = mode & 7;

    return ((bits & (uint32_t)want) == (uint32_t)want) ? 0 : -EACCES;
}

// ---------- paths ---------- //

// Turns an absolute path into canonical form: no //, no "." and no "..".
static int normalize(const char *path, char *out) {
    size_t len = 0;
    const char *p = path;

    if (!path || path[0] != '/') return -EINVAL;
    out[0] = '\0';

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != '/') p++;
        size_t clen = (size_t)(p - start);

        if (clen == 1 && start[0] == '.') continue;
        if (clen == 2 && start[0] == '.' && start[1] == '.') {
            while (len > 0 && out[len - 1] != '/') len--;   // drop the last component
            if (len > 0) len--;                             // and its leading slash
            out[len] = '\0';
            continue;
        }

        if (clen > VFS_NAME_MAX) return -ENAMETOOLONG;
        if (len + 1 + clen >= VFS_PATH_MAX) return -ENAMETOOLONG;

        out[len++] = '/';
        memcpy(out + len, start, clen);
        len += clen;
        out[len] = '\0';
    }

    if (len == 0) { out[0] = '/'; out[1] = '\0'; }
    return 0;
}

int vfs_normalize(const char *path, char *out) {
    return normalize(path, out);
}

static int is_mountpoint(const char *norm) {
    for (int i = 0; i < nmounts; i++)
        if (strcmp(mounts[i].path, norm) == 0) return 1;
    return 0;
}

// Finds the node for a path; returns it with a reference, or NULL and *err set.
static struct vfs_node *resolve(const char *path, int *err) {
    char norm[VFS_PATH_MAX];
    int r = normalize(path, norm);
    if (r) { *err = r; return NULL; }

    // Longest matching mount point wins
    struct vfs_mount *best = NULL;
    size_t bestlen = 0;
    for (int i = 0; i < nmounts; i++) {
        size_t ml = strlen(mounts[i].path);
        int match;
        if (ml == 1)
            match = 1;                                       // "/" matches everything
        else
            match = strncmp(norm, mounts[i].path, ml) == 0 &&
                    (norm[ml] == '\0' || norm[ml] == '/');
        if (match && ml >= bestlen) { best = &mounts[i]; bestlen = ml; }
    }
    if (!best) { *err = -ENOENT; return NULL; }

    const char *rest = norm + (bestlen == 1 ? 0 : bestlen);
    struct vfs_node *cur = best->root;
    cur->refs++;

    while (*rest) {
        while (*rest == '/') rest++;
        if (!*rest) break;

        char name[VFS_NAME_MAX + 1];
        size_t n = 0;
        while (*rest && *rest != '/') name[n++] = *rest++;
        name[n] = '\0';

        if (cur->type != VFS_DIR || !cur->ops || !cur->ops->finddir) {
            vfs_node_put(cur);
            *err = -ENOTDIR;
            return NULL;
        }

        // searching a directory needs its x permission
        if (vfs_access(cur, vfs_uid, vfs_gid, ACC_X) != 0) {
            vfs_node_put(cur);
            *err = -EACCES;
            return NULL;
        }

        struct vfs_node *next = cur->ops->finddir(cur, name);
        vfs_node_put(cur);
        if (!next) { *err = -ENOENT; return NULL; }
        cur = next;
    }
    return cur;
}

// Splits a normalized path ("/a/b/c") into its parent ("/a/b") and last component
static void split_parent(const char *norm, char *parent, const char **name) {
    size_t slash = strlen(norm);
    while (norm[slash] != '/') slash--;
    *name = norm + slash + 1;

    if (slash == 0) { parent[0] = '/'; parent[1] = '\0'; }
    else            { memcpy(parent, norm, slash); parent[slash] = '\0'; }
}

// Resolves the parent directory of a normalized path and checks that the caller
// may add or remove entries in it. *name points into `norm`.
static struct vfs_node *modifiable_parent(const char *norm, const char **name, int *err) {
    char parent[VFS_PATH_MAX];
    split_parent(norm, parent, name);

    struct vfs_node *dir = resolve(parent, err);
    if (!dir) return NULL;

    if (dir->type != VFS_DIR) {
        vfs_node_put(dir);
        *err = -ENOTDIR;
        return NULL;
    }

    int r = vfs_access(dir, vfs_uid, vfs_gid, ACC_W | ACC_X);
    if (r) {
        vfs_node_put(dir);
        *err = r;
        return NULL;
    }
    return dir;
}

// Creates a new file or directory at path; *out comes back with a reference.
static int create_node(const char *path, uint32_t type, struct vfs_node **out) {
    char norm[VFS_PATH_MAX];
    int r = normalize(path, norm);
    if (r) return r;
    if (norm[1] == '\0') return -EEXIST;                 // "/"

    const char *name;
    int err;
    struct vfs_node *dir = modifiable_parent(norm, &name, &err);
    if (!dir) return err;

    if (!dir->ops || !dir->ops->create || !dir->ops->finddir) {
        vfs_node_put(dir);
        return -EROFS;
    }

    struct vfs_node *existing = dir->ops->finddir(dir, name);
    if (existing) {
        vfs_node_put(existing);
        vfs_node_put(dir);
        return -EEXIST;
    }

    r = dir->ops->create(dir, name, type, out);
    if (r == 0) {                                        // the creator owns the new node
        struct vfs_node *nn = *out;
        if (nn->ops && nn->ops->setattr) {
            nn->ops->setattr(nn, -1, (int)vfs_uid, (int)vfs_gid);   // persisted on disk filesystems
        } else {
            nn->uid = (uint16_t)vfs_uid;
            nn->gid = (uint16_t)vfs_gid;
        }
    }
    vfs_node_put(dir);
    return r;
}

// ---------- mounting ---------- //

int vfs_mount(const char *path, struct vfs_node *root) {
    char norm[VFS_PATH_MAX];
    int r = normalize(path, norm);
    if (r) return r;
    if (!root) return -EINVAL;
    if (root->type != VFS_DIR) return -ENOTDIR;
    if (nmounts == VFS_MAX_MOUNTS) return -ENOSPC;

    for (int i = 0; i < nmounts; i++)
        if (strcmp(mounts[i].path, norm) == 0) return -EEXIST;

    memcpy(mounts[nmounts].path, norm, strlen(norm) + 1);
    mounts[nmounts].root = root;
    nmounts++;
    return 0;
}

int vfs_umount(const char *path) {
    char norm[VFS_PATH_MAX];
    int r = normalize(path, norm);
    if (r) return r;
    if (norm[1] == '\0') return -EBUSY;                     // never the root

    int idx = -1;
    for (int i = 0; i < nmounts; i++)
        if (strcmp(mounts[i].path, norm) == 0) idx = i;
    if (idx < 0) return -EINVAL;                            // not a mount point

    size_t len = strlen(norm);
    for (int i = 0; i < nmounts; i++) {                     // another mount lies below it
        if (i == idx) continue;
        if (strncmp(mounts[i].path, norm, len) == 0 && mounts[i].path[len] == '/') return -EBUSY;
    }

    struct vfs_node *root = mounts[idx].root;
    if (!root->ops || !root->ops->unmount) return -EINVAL;  // this filesystem cannot be unmounted
    r = root->ops->unmount(root);
    if (r) return r;

    for (int i = idx; i < nmounts - 1; i++) mounts[i] = mounts[i + 1];
    nmounts--;
    memset(&mounts[nmounts], 0, sizeof(mounts[0]));
    return 0;
}

int vfs_mount_path(int index, char *out) {
    if (index < 0 || index >= nmounts) return -1;
    memcpy(out, mounts[index].path, strlen(mounts[index].path) + 1);
    return 0;
}

// ---------- open file objects ---------- //

struct vfs_file *vfs_file_alloc(struct vfs_node *n, int flags) {
    struct vfs_file *f = kcalloc(1, sizeof(*f));
    if (!f) return NULL;

    n->refs++;                       // the file keeps its node alive
    f->node  = n;
    f->flags = flags;
    f->refs  = 1;
    return f;
}

void vfs_file_ref(struct vfs_file *f) {
    f->refs++;
}

void vfs_file_put(struct vfs_file *f) {
    if (!f) return;
    if (--f->refs > 0) return;

    vfs_node_put(f->node);
    kfree(f);
}

struct vfs_file *vfs_file_open(const char *path, int flags, int *err) {
    struct vfs_node *n = resolve(path, err);
    int created = 0;

    if (!n) {
        if (*err != -ENOENT || !(flags & O_CREAT)) return NULL;
        *err = create_node(path, VFS_FILE, &n);
        if (*err) return NULL;
        created = 1;
    }

    if (n->type == VFS_DIR && (flags & O_ACCMODE) != O_RDONLY) {
        vfs_node_put(n);
        *err = -EISDIR;
        return NULL;
    }

    if (!created) {
        int want = ACC_R;
        if ((flags & O_ACCMODE) == O_WRONLY)      want = ACC_W;
        else if ((flags & O_ACCMODE) == O_RDWR)   want = ACC_R | ACC_W;

        int r = vfs_access(n, vfs_uid, vfs_gid, want);
        if (r) {
            vfs_node_put(n);
            *err = r;
            return NULL;
        }
    }

    if ((flags & O_TRUNC) && n->type == VFS_FILE && (flags & O_ACCMODE) != O_RDONLY) {
        int r = (n->ops && n->ops->truncate) ? n->ops->truncate(n, 0) : -EROFS;
        if (r) {
            vfs_node_put(n);
            *err = r;
            return NULL;
        }
    }

    if (n->ops && n->ops->open) {
        struct vfs_node *repl = NULL;
        int r = n->ops->open(n, &repl);
        if (r) {
            vfs_node_put(n);
            *err = r;
            return NULL;
        }
        if (repl) {                  // the hook gave us a node of its own
            vfs_node_put(n);
            n = repl;
        }
    }

    struct vfs_file *f = vfs_file_alloc(n, flags);
    vfs_node_put(n);                 // drop the reference resolve/create gave us
    if (!f) *err = -ENOMEM;
    return f;
}

int vfs_file_read(struct vfs_file *f, void *buf, uint32_t len) {
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -EBADF;
    if (f->node->type == VFS_DIR) return -EISDIR;
    if (!f->node->ops || !f->node->ops->read) return -ENOSYS;
    if (len > 0x7FFFFFFFu) len = 0x7FFFFFFFu;

    int n = f->node->ops->read(f->node, f->offset, len, buf);
    if (n > 0) f->offset += (uint32_t)n;
    return n;
}

int vfs_file_write(struct vfs_file *f, const void *buf, uint32_t len) {
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -EBADF;
    if (f->node->type == VFS_DIR) return -EISDIR;
    if (!f->node->ops || !f->node->ops->write) return -EROFS;
    if (len > 0x7FFFFFFFu) len = 0x7FFFFFFFu;

    if (f->flags & O_APPEND) f->offset = f->node->size;

    int n = f->node->ops->write(f->node, f->offset, len, buf);
    if (n > 0) f->offset += (uint32_t)n;
    return n;
}

int32_t vfs_file_lseek(struct vfs_file *f, int32_t off, int whence) {
    int64_t base;
    switch (whence) {
    case SEEK_SET: base = 0;                 break;
    case SEEK_CUR: base = f->offset;         break;
    case SEEK_END: base = f->node->size;     break;
    default:       return -EINVAL;
    }

    int64_t pos = base + off;
    if (pos < 0 || pos > 0x7FFFFFFF) return -EINVAL;

    f->offset = (uint32_t)pos;
    return (int32_t)pos;
}

int vfs_file_readdir(struct vfs_file *f, struct vfs_dirent *out) {
    if (f->node->type != VFS_DIR) return -ENOTDIR;
    if (!f->node->ops || !f->node->ops->readdir) return -ENOSYS;

    int r = f->node->ops->readdir(f->node, f->offset, out);
    if (r > 0) f->offset++;
    return r;
}

// ---------- kernel-internal descriptors ---------- //

static struct vfs_file *get_file(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return NULL;
    return fds[fd];
}

static int alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FDS; i++)
        if (!fds[i]) return i;
    return -1;
}

int vfs_open(const char *path, int flags) {
    int fd = alloc_fd();
    if (fd < 0) return -EMFILE;

    int err = 0;
    struct vfs_file *f = vfs_file_open(path, flags, &err);
    if (!f) return err;

    fds[fd] = f;
    return fd;
}

int vfs_close(int fd) {
    struct vfs_file *f = get_file(fd);
    if (!f) return -EBADF;

    vfs_file_put(f);
    fds[fd] = NULL;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t len) {
    struct vfs_file *f = get_file(fd);
    return f ? vfs_file_read(f, buf, len) : -EBADF;
}

int vfs_write(int fd, const void *buf, uint32_t len) {
    struct vfs_file *f = get_file(fd);
    return f ? vfs_file_write(f, buf, len) : -EBADF;
}

int32_t vfs_lseek(int fd, int32_t off, int whence) {
    struct vfs_file *f = get_file(fd);
    return f ? vfs_file_lseek(f, off, whence) : -EBADF;
}

int vfs_readdir(int fd, struct vfs_dirent *out) {
    struct vfs_file *f = get_file(fd);
    return f ? vfs_file_readdir(f, out) : -EBADF;
}

// ---------- stat, mkdir, unlink, rmdir, rename ---------- //

int vfs_stat(const char *path, struct vfs_stat *st) {
    int err;
    struct vfs_node *n = resolve(path, &err);
    if (!n) return err;

    st->ino  = n->ino;
    st->type = n->type;
    st->size = n->size;
    st->mode = n->mode;
    st->uid  = n->uid;
    st->gid  = n->gid;
    vfs_node_put(n);
    return 0;
}

int vfs_mkdir(const char *path) {
    struct vfs_node *n;
    int r = create_node(path, VFS_DIR, &n);
    if (r) return r;
    vfs_node_put(n);
    return 0;
}

int vfs_unlink(const char *path) {
    char norm[VFS_PATH_MAX];
    int err = normalize(path, norm);
    if (err) return err;
    if (norm[1] == '\0' || is_mountpoint(norm)) return -EBUSY;

    const char *name;
    struct vfs_node *dir = modifiable_parent(norm, &name, &err);
    if (!dir) return err;

    int r;
    if (!dir->ops || !dir->ops->unlink) r = -EROFS;
    else                                r = dir->ops->unlink(dir, name);

    vfs_node_put(dir);
    return r;
}

int vfs_rmdir(const char *path) {
    char norm[VFS_PATH_MAX];
    int err = normalize(path, norm);
    if (err) return err;
    if (norm[1] == '\0' || is_mountpoint(norm)) return -EBUSY;

    const char *name;
    struct vfs_node *dir = modifiable_parent(norm, &name, &err);
    if (!dir) return err;

    int r;
    if (!dir->ops || !dir->ops->rmdir) r = -EROFS;
    else                               r = dir->ops->rmdir(dir, name);

    vfs_node_put(dir);
    return r;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    char onorm[VFS_PATH_MAX], nnorm[VFS_PATH_MAX];
    int err = normalize(oldpath, onorm);
    if (!err) err = normalize(newpath, nnorm);
    if (err) return err;

    if (onorm[1] == '\0' || nnorm[1] == '\0' || is_mountpoint(onorm) || is_mountpoint(nnorm))
        return -EBUSY;

    /* a directory can't be moved into itself */
    size_t ol = strlen(onorm);
    if (strncmp(nnorm, onorm, ol) == 0 && nnorm[ol] == '/') return -EINVAL;

    const char *oname, *nname;
    struct vfs_node *odir = modifiable_parent(onorm, &oname, &err);
    if (!odir) return err;
    struct vfs_node *ndir = modifiable_parent(nnorm, &nname, &err);
    if (!ndir) {
        vfs_node_put(odir);
        return err;
    }

    int r;
    if (odir->ops != ndir->ops)                r = -EXDEV;      // different filesystem types
    else if (!odir->ops || !odir->ops->rename) r = -EROFS;
    else r = odir->ops->rename(odir, oname, ndir, nname);

    vfs_node_put(ndir);
    vfs_node_put(odir);
    return r;
}

// ---------- permissions on paths ---------- //

int vfs_access_path(const char *path, int want) {
    int err;
    struct vfs_node *n = resolve(path, &err);
    if (!n) return err;

    int r = vfs_access(n, vfs_uid, vfs_gid, want);
    vfs_node_put(n);
    return r;
}

int vfs_chmod(const char *path, uint32_t mode) {
    int err, r;
    struct vfs_node *n = resolve(path, &err);
    if (!n) return err;

    if (vfs_uid != 0 && vfs_uid != n->uid) r = -EPERM;        // owner or root only
    else if (!n->ops || !n->ops->setattr)  r = -EROFS;
    else r = n->ops->setattr(n, (int)(mode & 07777), -1, -1);

    vfs_node_put(n);
    return r;
}

int vfs_chown(const char *path, int uid, int gid) {
    int err, r;
    struct vfs_node *n = resolve(path, &err);
    if (!n) return err;

    if (vfs_uid != 0)                      r = -EPERM;        // root only
    else if (!n->ops || !n->ops->setattr)  r = -EROFS;
    else r = n->ops->setattr(n, -1, uid, gid);

    vfs_node_put(n);
    return r;
}