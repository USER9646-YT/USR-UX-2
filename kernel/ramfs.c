#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "ramfs.h"
#include "vfs.h"
#include "kheap.h"

#define RAMFS_MAX_FILE (16u * 1024u * 1024u)

struct ramfs_entry {
    char                name[VFS_NAME_MAX + 1];
    struct vfs_node    *node;
    struct ramfs_entry *next;
};

struct ramfs_data {
    uint8_t            *data;         // files: contents
    uint32_t            cap;
    struct ramfs_entry *head, *tail;  // directories: children
};

static uint32_t next_ino = 100;
static struct vfs_ops ramfs_ops;      // filled in at the bottom

// ---------- directory list helpers ---------- //

static struct ramfs_entry *find_entry(struct ramfs_data *d, const char *name,
                                      struct ramfs_entry **prev_out) {
    struct ramfs_entry *prev = NULL;

    for (struct ramfs_entry *e = d->head; e; prev = e, e = e->next) {
        if (strcmp(e->name, name) == 0) {
            if (prev_out) *prev_out = prev;
            return e;
        }
    }
    return NULL;
}

static void unlink_entry(struct ramfs_data *d, struct ramfs_entry *e, struct ramfs_entry *prev) {
    if (prev) prev->next = e->next;
    else      d->head    = e->next;
    if (d->tail == e) d->tail = prev;
    e->next = NULL;
}

static void append_entry(struct ramfs_data *d, struct ramfs_entry *e) {
    e->next = NULL;
    if (d->tail) d->tail->next = e;
    else         d->head       = e;
    d->tail = e;
}

// ---------- nodes ---------- //

static struct vfs_node *ramfs_new_node(uint32_t type) {
    struct vfs_node   *n = kcalloc(1, sizeof(*n));
    struct ramfs_data *d = kcalloc(1, sizeof(*d));
    if (!n || !d) { kfree(n); kfree(d); return NULL; }

    n->ino  = next_ino++;
    n->type = type;
    n->refs = 1;
    n->mode = (type == VFS_DIR) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    n->ops  = &ramfs_ops;
    n->priv = d;
    return n;
}

// Called when the last reference goes away (open files keep an unlinked node alive)
static void ramfs_release(struct vfs_node *n) {
    struct ramfs_data *d = n->priv;

    kfree(d->data);
    kfree(d);                       // directories are only removed when empty
    kfree(n);
}

static int ensure_cap(struct ramfs_data *d, uint32_t end) {
    if (end > RAMFS_MAX_FILE) return -EFBIG;
    if (end <= d->cap) return 0;

    uint32_t newcap = d->cap * 2;
    if (newcap < end) newcap = end;
    if (newcap < 64)  newcap = 64;

    uint8_t *p = krealloc(d->data, newcap);
    if (!p) return -ENOMEM;
    d->data = p;
    d->cap  = newcap;
    return 0;
}

static int ramfs_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    struct ramfs_data *d = n->priv;

    if (off >= n->size) return 0;
    if (len > n->size - off) len = n->size - off;
    memcpy(buf, d->data + off, len);
    return (int)len;
}

static int ramfs_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    struct ramfs_data *d = n->priv;

    if (len == 0) return 0;
    uint32_t end = off + len;
    if (end < off) return -EFBIG;

    int r = ensure_cap(d, end);
    if (r) return r;

    if (off > n->size)                      // writing past EOF leaves a zero-filled gap
        memset(d->data + n->size, 0, off - n->size);

    memcpy(d->data + off, buf, len);
    if (end > n->size) n->size = end;
    return (int)len;
}

static int ramfs_truncate(struct vfs_node *n, uint32_t size) {
    struct ramfs_data *d = n->priv;

    if (n->type != VFS_FILE) return -EISDIR;
    if (size > n->size) {
        int r = ensure_cap(d, size);
        if (r) return r;
        memset(d->data + n->size, 0, size - n->size);
    }
    n->size = size;
    return 0;
}

static int ramfs_readdir(struct vfs_node *dir, uint32_t index, struct vfs_dirent *out) {
    struct ramfs_data *d = dir->priv;
    struct ramfs_entry *e = d->head;

    while (e && index--) e = e->next;
    if (!e) return 0;

    size_t len = strlen(e->name);
    memcpy(out->name, e->name, len + 1);
    out->ino  = e->node->ino;
    out->type = e->node->type;
    return 1;
}

static struct vfs_node *ramfs_finddir(struct vfs_node *dir, const char *name) {
    struct ramfs_entry *e = find_entry(dir->priv, name, NULL);
    if (!e) return NULL;

    e->node->refs++;
    return e->node;
}

static int ramfs_create(struct vfs_node *dir, const char *name, uint32_t type,
                        struct vfs_node **out) {
    struct ramfs_data *dd = dir->priv;
    size_t len = strlen(name);

    if (len == 0 || len > VFS_NAME_MAX) return -ENAMETOOLONG;

    struct ramfs_entry *e = kcalloc(1, sizeof(*e));
    struct vfs_node    *n = ramfs_new_node(type);
    if (!e || !n) {
        kfree(e);
        if (n) { kfree(n->priv); kfree(n); }
        return -ENOMEM;
    }

    memcpy(e->name, name, len + 1);
    e->node = n;
    n->refs = 2;                    // one for the directory entry, one for the caller
    append_entry(dd, e);

    *out = n;
    return 0;
}

static int ramfs_unlink(struct vfs_node *dir, const char *name) {
    struct ramfs_data *dd = dir->priv;
    struct ramfs_entry *prev, *e = find_entry(dd, name, &prev);

    if (!e) return -ENOENT;
    if (e->node->type == VFS_DIR) return -EISDIR;

    struct vfs_node *n = e->node;
    unlink_entry(dd, e, prev);
    kfree(e);
    vfs_node_put(n);                // drop the directory's reference
    return 0;
}

static int ramfs_rmdir(struct vfs_node *dir, const char *name) {
    struct ramfs_data *dd = dir->priv;
    struct ramfs_entry *prev, *e = find_entry(dd, name, &prev);

    if (!e) return -ENOENT;
    if (e->node->type != VFS_DIR) return -ENOTDIR;
    if (((struct ramfs_data *)e->node->priv)->head) return -ENOTEMPTY;

    struct vfs_node *n = e->node;
    unlink_entry(dd, e, prev);
    kfree(e);
    vfs_node_put(n);
    return 0;
}

static int ramfs_rename(struct vfs_node *odir, const char *oname,
                        struct vfs_node *ndir, const char *nname) {
    struct ramfs_data *od = odir->priv, *nd = ndir->priv;
    size_t len = strlen(nname);

    if (len == 0 || len > VFS_NAME_MAX) return -ENAMETOOLONG;

    struct ramfs_entry *oprev, *e = find_entry(od, oname, &oprev);
    if (!e) return -ENOENT;

    struct ramfs_entry *tprev, *t = find_entry(nd, nname, &tprev);
    if (t == e) return 0;                               // renaming onto itself

    if (t) {                                            // replace an existing file
        if (t->node->type == VFS_DIR || e->node->type == VFS_DIR) return -EEXIST;

        struct vfs_node *old = t->node;
        unlink_entry(nd, t, tprev);
        kfree(t);
        vfs_node_put(old);

        e = find_entry(od, oname, &oprev);              // the list may have changed
    }

    unlink_entry(od, e, oprev);
    memcpy(e->name, nname, len + 1);
    append_entry(nd, e);
    return 0;
}

static int ramfs_setattr(struct vfs_node *n, int mode, int uid, int gid) {
    if (mode >= 0) n->mode = (uint16_t)((n->mode & S_IFMT) | (mode & 07777));
    if (uid >= 0)  n->uid  = (uint16_t)uid;
    if (gid >= 0)  n->gid  = (uint16_t)gid;
    return 0;
}

// Is anything below this directory still referenced from outside the tree?
static int tree_busy(struct vfs_node *dir) {
    struct ramfs_data *d = dir->priv;

    for (struct ramfs_entry *e = d->head; e; e = e->next) {
        struct vfs_node *n = e->node;
        if (n->refs > 1) return 1;                      // open file, cwd, or a node shared with another fs
        if (n->type == VFS_DIR && n->ops == &ramfs_ops && tree_busy(n)) return 1;
    }
    return 0;
}

static void tree_free(struct vfs_node *dir) {
    struct ramfs_data *d = dir->priv;

    while (d->head) {
        struct ramfs_entry *e = d->head;
        struct vfs_node *n = e->node;
        unlink_entry(d, e, NULL);
        kfree(e);
        if (n->type == VFS_DIR && n->ops == &ramfs_ops) tree_free(n);
        vfs_node_put(n);
    }
}

// Like ext2_unmount: refuses while busy, otherwise consumes the root node
static int ramfs_unmount(struct vfs_node *root) {
    if (root->refs > 1) return -EBUSY;
    if (tree_busy(root)) return -EBUSY;
    tree_free(root);
    vfs_node_put(root);                                 // last reference -> ramfs_release
    return 0;
}

static struct vfs_ops ramfs_ops = {
    .read     = ramfs_read,
    .write    = ramfs_write,
    .readdir  = ramfs_readdir,
    .finddir  = ramfs_finddir,
    .create   = ramfs_create,
    .release  = ramfs_release,
    .setattr  = ramfs_setattr,
    .unlink   = ramfs_unlink,
    .rmdir    = ramfs_rmdir,
    .rename   = ramfs_rename,
    .truncate = ramfs_truncate,
};

int ramfs_add_node(struct vfs_node *dir, const char *name, struct vfs_node *node) {
    if (!dir || !node || dir->ops != &ramfs_ops) return -EINVAL;

    size_t len = strlen(name);
    if (len == 0 || len > VFS_NAME_MAX) return -ENAMETOOLONG;

    struct ramfs_data *dd = dir->priv;
    if (find_entry(dd, name, NULL)) return -EEXIST;

    struct ramfs_entry *e = kcalloc(1, sizeof(*e));
    if (!e) return -ENOMEM;

    memcpy(e->name, name, len + 1);
    node->refs++;                   // the directory owns a reference
    e->node = node;
    append_entry(dd, e);
    return 0;
}

struct vfs_node *ramfs_create_root(void) {
    struct vfs_node *n = ramfs_new_node(VFS_DIR);
    if (n) n->mode = S_IFDIR | 01777;       // like /tmp: everyone may create entries
    return n;
}