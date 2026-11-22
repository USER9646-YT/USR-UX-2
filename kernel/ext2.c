#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "vfs.h"
#include "ata.h"
#include "pit.h"
#include "kheap.h"
#include "kprintf.h"

#define EXT2_MAGIC         0xEF53
#define EXT2_ROOT_INO      2
#define INCOMPAT_FILETYPE  0x0002
#define EXT2_INDEX_FL      0x00001000u

#define S_IFMT   0xF000
#define S_IFREG  0x8000
#define S_IFDIR  0x4000
#define S_IFLNK  0xA000

#define FT_REG     1
#define FT_DIR     2
#define FT_SYMLINK 7

// ---------- on-disk structures (little-endian, same as the CPU) ---------- //

struct ext2_super {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;          // block size = 1024 << this
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    // revision 1 and later
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
} __attribute__((packed));

struct ext2_gd {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t used_dirs;
    uint16_t pad;
    uint8_t  reserved[12];
} __attribute__((packed));

struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links;
    uint32_t blocks;                  // in 512-byte units
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];               // 12 direct, then single/double/triple indirect
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint8_t  osd2[12];
} __attribute__((packed));

struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    // name follows, NOT null-terminated
} __attribute__((packed));

_Static_assert(sizeof(struct ext2_super)  == 104, "ext2_super layout");
_Static_assert(sizeof(struct ext2_gd)     == 32,  "ext2_gd layout");
_Static_assert(sizeof(struct ext2_inode)  == 128, "ext2_inode layout");
_Static_assert(sizeof(struct ext2_dirent) == 8,   "ext2_dirent layout");

// ---------- in-memory state ---------- //

struct ext2_node;

struct ext2_fs {
    int      drive;
    uint32_t start_lba;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t first_data_block;
    uint32_t groups;
    int      has_filetype;

    struct ext2_super *sb;            // the 1024-byte superblock, kept in memory
    struct ext2_gd    *gdt;           // the whole group descriptor table
    uint32_t           gdt_blocks;
    int                meta_dirty;    // superblock / descriptors need writing

    struct ext2_node  *nodes;         // every inode that is currently referenced
};

struct ext2_node {                    // vfs_node->priv
    struct ext2_fs    *fs;
    struct ext2_inode  inode;
    struct vfs_node   *vnode;
    struct ext2_node  *next;
};

static struct vfs_ops ext2_ops;       // defined at the bottom

// No clock yet: a fixed base plus the uptime keeps timestamps increasing
static uint32_t now(void) {
    return 1767225600u + pit_ticks() / 100u;
}

// ---------- low-level block access ---------- //

static int read_block(struct ext2_fs *fs, uint32_t blk, void *buf) {
    if (blk >= fs->blocks_count) return -EIO;
    if (ata_read(fs->drive, fs->start_lba + blk * fs->sectors_per_block,
                 fs->sectors_per_block, buf) != 0)
        return -EIO;
    return 0;
}

static int write_block(struct ext2_fs *fs, uint32_t blk, const void *buf) {
    if (blk >= fs->blocks_count) return -EIO;
    if (ata_write(fs->drive, fs->start_lba + blk * fs->sectors_per_block,
                  fs->sectors_per_block, buf) != 0)
        return -EIO;
    return 0;
}

static int zero_block(struct ext2_fs *fs, uint32_t blk) {
    uint8_t *z = kcalloc(1, fs->block_size);
    if (!z) return -ENOMEM;
    int r = write_block(fs, blk, z);
    kfree(z);
    return r;
}

// Writes the superblock and group descriptors back if the free counts changed
static int flush_meta(struct ext2_fs *fs) {
    if (!fs->meta_dirty) return 0;

    if (ata_write(fs->drive, fs->start_lba + 2, 2, fs->sb) != 0) return -EIO;

    for (uint32_t i = 0; i < fs->gdt_blocks; i++) {
        int r = write_block(fs, fs->first_data_block + 1 + i,
                            (uint8_t *)fs->gdt + i * fs->block_size);
        if (r) return r;
    }
    fs->meta_dirty = 0;
    return 0;
}

// ---------- inodes on disk ---------- //

static int inode_loc(struct ext2_fs *fs, uint32_t ino, uint32_t *blk, uint32_t *off) {
    if (ino == 0 || ino > fs->inodes_count) return -EINVAL;

    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    if (group >= fs->groups) return -EIO;

    uint32_t byte = index * fs->inode_size;
    *blk = fs->gdt[group].inode_table + byte / fs->block_size;
    *off = byte % fs->block_size;
    return 0;
}

static int read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out) {
    uint32_t blk, off;
    int r = inode_loc(fs, ino, &blk, &off);
    if (r) return r;

    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;

    r = read_block(fs, blk, buf);
    if (r == 0) memcpy(out, buf + off, sizeof(*out));
    kfree(buf);
    return r;
}

static int write_inode(struct ext2_fs *fs, uint32_t ino, const struct ext2_inode *in) {
    uint32_t blk, off;
    int r = inode_loc(fs, ino, &blk, &off);
    if (r) return r;

    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;

    r = read_block(fs, blk, buf);
    if (r == 0) {
        memcpy(buf + off, in, sizeof(*in));         // only the first 128 bytes are ours
        r = write_block(fs, blk, buf);
    }
    kfree(buf);
    return r;
}

static uint32_t mode_to_type(uint16_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG: return VFS_FILE;
    case S_IFDIR: return VFS_DIR;
    case S_IFLNK: return VFS_SYMLINK;
    default:      return VFS_OTHER;
    }
}

static uint32_t ft_of(uint32_t type) {
    switch (type) {
    case VFS_DIR:     return FT_DIR;
    case VFS_SYMLINK: return FT_SYMLINK;
    default:          return FT_REG;
    }
}

// ---------- bitmaps and allocation ---------- //

// Finds a clear bit among the first nbits of a bitmap block, sets it and writes it back
static int bitmap_alloc(struct ext2_fs *fs, uint32_t bitmap_blk, uint32_t nbits, uint32_t *bit_out) {
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;

    int r = read_block(fs, bitmap_blk, buf);
    if (r) { kfree(buf); return r; }

    for (uint32_t i = 0; i < nbits; i++) {
        if (buf[i >> 3] == 0xFF) { i |= 7; continue; }          // skip full bytes

        if (!(buf[i >> 3] & (1u << (i & 7)))) {
            buf[i >> 3] |= (uint8_t)(1u << (i & 7));
            r = write_block(fs, bitmap_blk, buf);
            kfree(buf);
            if (r) return r;
            *bit_out = i;
            return 0;
        }
    }

    kfree(buf);
    return -ENOSPC;
}

static int bitmap_clear(struct ext2_fs *fs, uint32_t bitmap_blk, uint32_t bit) {
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;

    int r = read_block(fs, bitmap_blk, buf);
    if (r == 0) {
        buf[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
        r = write_block(fs, bitmap_blk, buf);
    }
    kfree(buf);
    return r;
}

static int alloc_block(struct ext2_fs *fs, uint32_t goal_group, uint32_t *out) {
    for (uint32_t k = 0; k < fs->groups; k++) {
        uint32_t g = (goal_group + k) % fs->groups;
        if (fs->gdt[g].free_blocks == 0) continue;

        uint32_t first = fs->first_data_block + g * fs->blocks_per_group;
        uint32_t nbits = fs->blocks_count - first;
        if (nbits > fs->blocks_per_group) nbits = fs->blocks_per_group;

        uint32_t bit;
        int r = bitmap_alloc(fs, fs->gdt[g].block_bitmap, nbits, &bit);
        if (r == -ENOSPC) continue;                 // the descriptor's counter was stale
        if (r) return r;

        fs->gdt[g].free_blocks--;
        fs->sb->free_blocks_count--;
        fs->meta_dirty = 1;
        *out = first + bit;
        return 0;
    }
    return -ENOSPC;
}

static void free_block(struct ext2_fs *fs, uint32_t blk) {
    if (blk < fs->first_data_block || blk >= fs->blocks_count) return;

    uint32_t rel = blk - fs->first_data_block;
    uint32_t g   = rel / fs->blocks_per_group;
    uint32_t bit = rel % fs->blocks_per_group;
    if (g >= fs->groups) return;

    if (bitmap_clear(fs, fs->gdt[g].block_bitmap, bit) != 0) return;

    fs->gdt[g].free_blocks++;
    fs->sb->free_blocks_count++;
    fs->meta_dirty = 1;
}

static int alloc_inode(struct ext2_fs *fs, uint32_t goal_group, int isdir, uint32_t *out) {
    for (uint32_t k = 0; k < fs->groups; k++) {
        uint32_t g = (goal_group + k) % fs->groups;
        if (fs->gdt[g].free_inodes == 0) continue;

        uint32_t bit;
        int r = bitmap_alloc(fs, fs->gdt[g].inode_bitmap, fs->inodes_per_group, &bit);
        if (r == -ENOSPC) continue;
        if (r) return r;

        fs->gdt[g].free_inodes--;
        fs->sb->free_inodes_count--;
        if (isdir) fs->gdt[g].used_dirs++;
        fs->meta_dirty = 1;
        *out = g * fs->inodes_per_group + bit + 1;
        return 0;
    }
    return -ENOSPC;
}

static void free_inode(struct ext2_fs *fs, uint32_t ino, int isdir) {
    if (ino == 0 || ino > fs->inodes_count) return;

    uint32_t g   = (ino - 1) / fs->inodes_per_group;
    uint32_t bit = (ino - 1) % fs->inodes_per_group;
    if (g >= fs->groups) return;

    if (bitmap_clear(fs, fs->gdt[g].inode_bitmap, bit) != 0) return;

    fs->gdt[g].free_inodes++;
    fs->sb->free_inodes_count++;
    if (isdir && fs->gdt[g].used_dirs > 0) fs->gdt[g].used_dirs--;
    fs->meta_dirty = 1;
}

// ---------- mapping file blocks to disk blocks ---------- //

// Follows `level` levels of indirection starting at block `blk`.
// *out = 0 means a hole.
static int walk_indirect(struct ext2_fs *fs, uint32_t blk, int level,
                         uint32_t idx, uint32_t *out) {
    uint32_t ppb = fs->block_size / 4;          // pointers per block
    uint32_t div = 1;
    for (int i = 1; i < level; i++) div *= ppb;

    uint8_t *buf = NULL;
    for (; level > 0; level--) {
        if (blk == 0) { *out = 0; kfree(buf); return 0; }
        if (!buf) {
            buf = kmalloc(fs->block_size);
            if (!buf) return -ENOMEM;
        }
        int r = read_block(fs, blk, buf);
        if (r) { kfree(buf); return r; }

        uint32_t slot = idx / div;
        idx %= div;
        if (slot >= ppb) { kfree(buf); return -EIO; }
        blk = ((uint32_t *)buf)[slot];
        div = div > 1 ? div / ppb : 1;
    }

    *out = blk;
    kfree(buf);
    return 0;
}

// Maps file block number `fb` to a disk block number (0 = hole), without allocating
static int bmap(struct ext2_fs *fs, const struct ext2_inode *ino,
                uint32_t fb, uint32_t *out) {
    uint32_t ppb = fs->block_size / 4;

    if (fb < 12) { *out = ino->block[fb]; return 0; }
    fb -= 12;
    if (fb < ppb) return walk_indirect(fs, ino->block[12], 1, fb, out);
    fb -= ppb;
    if (fb < ppb * ppb) return walk_indirect(fs, ino->block[13], 2, fb, out);
    fb -= ppb * ppb;
    return walk_indirect(fs, ino->block[14], 3, fb, out);
}

// Like bmap, but allocates the data block (and any indirect blocks) if missing.
// *created = 1 means the returned block is brand new and its contents are garbage.
static int alloc_fileblock(struct ext2_fs *fs, struct ext2_node *en, uint32_t fb,
                           uint32_t *out, int *created) {
    struct ext2_inode *in = &en->inode;
    uint32_t ppb  = fs->block_size / 4;
    uint32_t spb  = fs->sectors_per_block;
    uint32_t goal = (en->vnode->ino - 1) / fs->inodes_per_group;
    uint32_t b;
    int r;

    *created = 0;

    if (fb < 12) {
        if (in->block[fb]) { *out = in->block[fb]; return 0; }
        r = alloc_block(fs, goal, &b);
        if (r) return r;
        in->block[fb] = b;
        in->blocks += spb;
        *out = b;
        *created = 1;
        return 0;
    }

    uint32_t *rootp;
    int level;

    fb -= 12;
    if (fb < ppb) {
        level = 1;
        rootp = &in->block[12];
    } else if (fb - ppb < ppb * ppb) {
        fb -= ppb;
        level = 2;
        rootp = &in->block[13];
    } else {
        fb -= ppb + ppb * ppb;
        level = 3;
        rootp = &in->block[14];
    }

    if (!*rootp) {                                  // the top-level indirect block
        r = alloc_block(fs, goal, &b);
        if (r) return r;
        r = zero_block(fs, b);
        if (r) { free_block(fs, b); return r; }
        *rootp = b;
        in->blocks += spb;
    }

    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;

    uint32_t cur = *rootp, idx = fb, div = 1;
    for (int i = 1; i < level; i++) div *= ppb;
    r = 0;

    for (; level > 0; level--) {
        r = read_block(fs, cur, buf);
        if (r) break;

        uint32_t slot = idx / div;
        idx %= div;
        if (slot >= ppb) { r = -EFBIG; break; }

        uint32_t *ptrs = (uint32_t *)buf;
        uint32_t next = ptrs[slot];

        if (level == 1) {                           // this slot holds the data block
            if (next) { *out = next; break; }

            r = alloc_block(fs, goal, &b);
            if (r) break;
            ptrs[slot] = b;
            r = write_block(fs, cur, buf);
            if (r) { free_block(fs, b); break; }
            in->blocks += spb;
            *out = b;
            *created = 1;
            break;
        }

        if (!next) {                                // a missing intermediate block
            r = alloc_block(fs, goal, &b);
            if (r) break;
            r = zero_block(fs, b);
            if (r) { free_block(fs, b); break; }
            ptrs[slot] = b;
            r = write_block(fs, cur, buf);
            if (r) { free_block(fs, b); break; }
            in->blocks += spb;
            next = b;
        }

        cur = next;
        div = div > 1 ? div / ppb : 1;
    }

    kfree(buf);
    return r;
}

// Frees everything under indirect block `blk` whose file block number is >= first.
// `base` is the file block number of its first slot. Returns 1 if the block itself
// became empty and was freed. */
static int free_ind(struct ext2_fs *fs, uint32_t blk, int level, uint32_t base,
                    uint32_t first, uint32_t *nfreed) {
    uint32_t ppb  = fs->block_size / 4;
    uint32_t span = 1;
    for (int i = 1; i < level; i++) span *= ppb;

    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return 0;
    if (read_block(fs, blk, buf) != 0) { kfree(buf); return 0; }

    uint32_t *ptrs = (uint32_t *)buf;
    int dirty = 0, empty = 1;

    for (uint32_t i = 0; i < ppb; i++) {
        if (!ptrs[i]) continue;

        uint32_t sbase = base + i * span;
        if (sbase + span <= first) { empty = 0; continue; }     // entirely kept

        if (level == 1) {
            free_block(fs, ptrs[i]);
            ptrs[i] = 0;
            dirty = 1;
            (*nfreed)++;
        } else if (free_ind(fs, ptrs[i], level - 1, sbase, first, nfreed)) {
            ptrs[i] = 0;
            dirty = 1;
        } else {
            empty = 0;
        }
    }

    if (empty) {
        free_block(fs, blk);
        (*nfreed)++;
        kfree(buf);
        return 1;
    }

    if (dirty) write_block(fs, blk, buf);
    kfree(buf);
    return 0;
}

// Frees every block of the file with index >= first
static void free_from(struct ext2_fs *fs, struct ext2_node *en, uint32_t first) {
    struct ext2_inode *in = &en->inode;
    uint32_t ppb = fs->block_size / 4;
    uint32_t nf = 0;

    for (uint32_t i = first; i < 12; i++) {
        if (in->block[i]) {
            free_block(fs, in->block[i]);
            in->block[i] = 0;
            nf++;
        }
    }
    if (in->block[12] && free_ind(fs, in->block[12], 1, 12, first, &nf))
        in->block[12] = 0;
    if (in->block[13] && free_ind(fs, in->block[13], 2, 12 + ppb, first, &nf))
        in->block[13] = 0;
    if (in->block[14] && free_ind(fs, in->block[14], 3, 12 + ppb + ppb * ppb, first, &nf))
        in->block[14] = 0;

    uint32_t sub = nf * fs->sectors_per_block;
    in->blocks = in->blocks > sub ? in->blocks - sub : 0;
}

// ---------- vfs nodes (one shared node per inode) ---------- //

static struct vfs_node *make_node(struct ext2_fs *fs, uint32_t ino) {
    for (struct ext2_node *e = fs->nodes; e; e = e->next) {
        if (e->vnode->ino == ino) {
            e->vnode->refs++;
            return e->vnode;
        }
    }

    struct ext2_node *en = kcalloc(1, sizeof(*en));
    struct vfs_node  *n  = kcalloc(1, sizeof(*n));

    if (!en || !n || read_inode(fs, ino, &en->inode) != 0) {
        kfree(en);
        kfree(n);
        return NULL;
    }

    en->fs    = fs;
    en->vnode = n;

    n->ino  = ino;
    n->type = mode_to_type(en->inode.mode);
    n->size = en->inode.size;       // high 32 bits (files > 4 GiB) are ignored
    n->mode = en->inode.mode;
    n->uid  = en->inode.uid;
    n->gid  = en->inode.gid;
    n->refs = 1;
    n->ops  = &ext2_ops;
    n->priv = en;

    en->next  = fs->nodes;
    fs->nodes = en;
    return n;
}

// Called when the last reference goes away. An inode that was unlinked while
// still open gets its storage freed here.
static void ext2_release(struct vfs_node *n) {
    struct ext2_node *en = n->priv;
    struct ext2_fs *fs = en->fs;

    for (struct ext2_node **pp = &fs->nodes; *pp; pp = &(*pp)->next) {
        if (*pp == en) {
            *pp = en->next;
            break;
        }
    }

    if (en->inode.links == 0) {
        int has_blocks = n->type == VFS_FILE || n->type == VFS_DIR ||
                         (n->type == VFS_SYMLINK && en->inode.blocks != 0);
        if (has_blocks) free_from(fs, en, 0);

        en->inode.dtime = now();
        write_inode(fs, n->ino, &en->inode);
        free_inode(fs, n->ino, n->type == VFS_DIR);
        flush_meta(fs);
    }

    kfree(en);
    kfree(n);
}

// ---------- directories ---------- //

// Return 0 to continue, >0 to stop (value is passed back), <0 for an error.
typedef int (*dir_cb)(struct ext2_fs *fs, const struct ext2_dirent *de,
                      const char *name, void *ctx);

static int dir_walk(struct ext2_node *dn, dir_cb cb, void *ctx) {
    struct ext2_fs *fs = dn->fs;
    uint32_t bs = fs->block_size;
    uint32_t nblocks = (dn->inode.size + bs - 1) / bs;
    int result = 0;

    uint8_t *buf = kmalloc(bs);
    if (!buf) return -ENOMEM;

    for (uint32_t fb = 0; fb < nblocks && !result; fb++) {
        uint32_t disk;
        int r = bmap(fs, &dn->inode, fb, &disk);
        if (r) { result = r; break; }
        if (!disk) continue;

        r = read_block(fs, disk, buf);
        if (r) { result = r; break; }

        uint32_t off = 0;
        while (off + sizeof(struct ext2_dirent) <= bs) {
            const struct ext2_dirent *de = (const struct ext2_dirent *)(buf + off);

            if (de->rec_len < sizeof(*de) || off + de->rec_len > bs ||
                (uint32_t)de->name_len + sizeof(*de) > de->rec_len) {
                result = -EIO;                          // corrupt directory
                break;
            }
            if (de->inode) {
                int c = cb(fs, de, (const char *)(de + 1), ctx);
                if (c) { result = c; break; }
            }
            off += de->rec_len;
        }
    }

    kfree(buf);
    return result;
}

struct find_ctx {
    const char *name;
    size_t      len;
    uint32_t    ino;
};

static int find_cb(struct ext2_fs *fs, const struct ext2_dirent *de,
                   const char *name, void *c) {
    struct find_ctx *x = c;
    (void)fs;

    if (de->name_len == x->len && memcmp(name, x->name, x->len) == 0) {
        x->ino = de->inode;
        return 1;
    }
    return 0;
}

static int empty_cb(struct ext2_fs *fs, const struct ext2_dirent *de,
                    const char *name, void *c) {
    (void)fs; (void)c;

    if (de->name_len == 1 && name[0] == '.') return 0;
    if (de->name_len == 2 && name[0] == '.' && name[1] == '.') return 0;
    return 1;                                           /* a real entry */
}

static uint32_t dirent_type(struct ext2_fs *fs, const struct ext2_dirent *de) {
    switch (de->file_type) {
    case 1:  return VFS_FILE;
    case 2:  return VFS_DIR;
    case 7:  return VFS_SYMLINK;
    case 0:  break;                  // filetype feature off: look at the inode
    default: return VFS_OTHER;
    }

    struct ext2_inode ino;
    if (read_inode(fs, de->inode, &ino) == 0) return mode_to_type(ino.mode);
    return VFS_OTHER;
}

struct list_ctx {
    uint32_t           target;
    uint32_t           seen;
    struct vfs_dirent *out;
};

static int list_cb(struct ext2_fs *fs, const struct ext2_dirent *de,
                   const char *name, void *c) {
    struct list_ctx *x = c;

    if (x->seen++ != x->target) return 0;

    size_t len = de->name_len;
    if (len > VFS_NAME_MAX) len = VFS_NAME_MAX;
    memcpy(x->out->name, name, len);
    x->out->name[len] = '\0';
    x->out->ino  = de->inode;
    x->out->type = dirent_type(fs, de);
    return 1;
}

// Adds an entry to a directory, reusing free space or appending a block
static int dir_add(struct ext2_fs *fs, struct ext2_node *dn, const char *name,
                   uint32_t ino, uint32_t ftype) {
    uint32_t bs = fs->block_size;
    uint32_t len = (uint32_t)strlen(name);
    uint32_t need = (8 + len + 3) & ~3u;
    uint32_t nblocks = dn->inode.size / bs;
    int r = 0;

    uint8_t *buf = kmalloc(bs);
    if (!buf) return -ENOMEM;

    // We don't maintain htree indexes: turn the directory into a plain linear one
    dn->inode.flags &= ~EXT2_INDEX_FL;

    for (uint32_t fb = 0; fb < nblocks; fb++) {
        uint32_t disk;
        r = bmap(fs, &dn->inode, fb, &disk);
        if (r) goto out;
        if (!disk) continue;

        r = read_block(fs, disk, buf);
        if (r) goto out;

        uint32_t off = 0;
        while (off + sizeof(struct ext2_dirent) <= bs) {
            struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

            if (de->rec_len < sizeof(*de) || off + de->rec_len > bs) { r = -EIO; goto out; }

            uint32_t used = de->inode ? ((8u + de->name_len + 3u) & ~3u) : 0;
            if (de->rec_len >= used + need) {
                struct ext2_dirent *ne;

                if (used == 0) {
                    ne = de;                            // reuse a free entry as it is
                } else {                                // split the slack off this entry
                    ne = (struct ext2_dirent *)(buf + off + used);
                    ne->rec_len = (uint16_t)(de->rec_len - used);
                    de->rec_len = (uint16_t)used;
                }
                ne->inode     = ino;
                ne->name_len  = (uint8_t)len;
                ne->file_type = fs->has_filetype ? (uint8_t)ftype : 0;
                memcpy((uint8_t *)(ne + 1), name, len);

                r = write_block(fs, disk, buf);
                goto out;
            }
            off += de->rec_len;
        }
    }

    {   // no room anywhere: append a new directory block
        uint32_t disk;
        int created;

        r = alloc_fileblock(fs, dn, nblocks, &disk, &created);
        if (r) goto out;

        memset(buf, 0, bs);
        struct ext2_dirent *ne = (struct ext2_dirent *)buf;
        ne->inode     = ino;
        ne->rec_len   = (uint16_t)bs;
        ne->name_len  = (uint8_t)len;
        ne->file_type = fs->has_filetype ? (uint8_t)ftype : 0;
        memcpy(buf + 8, name, len);

        r = write_block(fs, disk, buf);
        if (r) goto out;

        dn->inode.size += bs;
        dn->vnode->size = dn->inode.size;
    }

out:
    kfree(buf);
    if (r == 0) {
        dn->inode.mtime = now();
        r = write_inode(fs, dn->vnode->ino, &dn->inode);
    }
    return r;
}

// Removes an entry; *ino_out is the inode it pointed to
static int dir_remove(struct ext2_fs *fs, struct ext2_node *dn, const char *name,
                      uint32_t *ino_out) {
    uint32_t bs = fs->block_size;
    uint32_t len = (uint32_t)strlen(name);
    uint32_t nblocks = dn->inode.size / bs;

    uint8_t *buf = kmalloc(bs);
    if (!buf) return -ENOMEM;

    for (uint32_t fb = 0; fb < nblocks; fb++) {
        uint32_t disk;
        int r = bmap(fs, &dn->inode, fb, &disk);
        if (r) { kfree(buf); return r; }
        if (!disk) continue;

        r = read_block(fs, disk, buf);
        if (r) { kfree(buf); return r; }

        struct ext2_dirent *prev = NULL;
        uint32_t off = 0;
        while (off + sizeof(struct ext2_dirent) <= bs) {
            struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);

            if (de->rec_len < sizeof(*de) || off + de->rec_len > bs) { kfree(buf); return -EIO; }

            if (de->inode && de->name_len == len &&
                memcmp((uint8_t *)(de + 1), name, len) == 0) {
                *ino_out = de->inode;
                if (prev) prev->rec_len = (uint16_t)(prev->rec_len + de->rec_len);
                else      de->inode = 0;                // first entry of the block: just free it //

                r = write_block(fs, disk, buf);
                kfree(buf);
                return r;
            }
            prev = de;
            off += de->rec_len;
        }
    }

    kfree(buf);
    return -ENOENT;
}

// Points a directory's ".." entry at a new parent
static int set_dotdot(struct ext2_fs *fs, struct ext2_node *dn, uint32_t parent_ino) {
    uint32_t bs = fs->block_size;
    uint32_t disk = dn->inode.block[0];
    if (!disk) return -EIO;

    uint8_t *buf = kmalloc(bs);
    if (!buf) return -ENOMEM;

    int r = read_block(fs, disk, buf);
    if (r == 0) {
        struct ext2_dirent *d1 = (struct ext2_dirent *)buf;
        if (d1->rec_len < 12 || d1->rec_len >= bs) {
            r = -EIO;
        } else {
            struct ext2_dirent *d2 = (struct ext2_dirent *)(buf + d1->rec_len);
            d2->inode = parent_ino;
            r = write_block(fs, disk, buf);
        }
    }
    kfree(buf);
    return r;
}

// ---------- vfs operations ---------- //

static struct vfs_node *ext2_finddir(struct vfs_node *dir, const char *name) {
    struct ext2_node *dn = dir->priv;
    struct find_ctx ctx = { name, strlen(name), 0 };

    if (dir_walk(dn, find_cb, &ctx) != 1) return NULL;
    return make_node(dn->fs, ctx.ino);
}

static int ext2_readdir(struct vfs_node *dir, uint32_t index, struct vfs_dirent *out) {
    struct ext2_node *dn = dir->priv;
    struct list_ctx ctx = { index, 0, out };

    return dir_walk(dn, list_cb, &ctx);     // 1 = entry, 0 = end, <0 = error
}

static int ext2_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    struct ext2_node *en = n->priv;
    struct ext2_fs   *fs = en->fs;
    uint32_t bs = fs->block_size;

    if (n->type == VFS_OTHER) return -ENODEV;       // device nodes etc.
    if (off >= n->size) return 0;
    if (len > n->size - off) len = n->size - off;

    // Fast symlink: the target path lives inside the inode itself
    if (n->type == VFS_SYMLINK && en->inode.blocks == 0) {
        memcpy(buf, (uint8_t *)en->inode.block + off, len);
        return (int)len;
    }

    uint8_t *tmp = kmalloc(bs);
    if (!tmp) return -ENOMEM;

    uint8_t *dst = buf;
    uint32_t done = 0;

    while (done < len) {
        uint32_t pos   = off + done;
        uint32_t fb    = pos / bs;
        uint32_t inblk = pos % bs;
        uint32_t chunk = bs - inblk;
        if (chunk > len - done) chunk = len - done;

        uint32_t disk;
        int r = bmap(fs, &en->inode, fb, &disk);
        if (r == 0) {
            if (disk == 0) {
                memset(dst + done, 0, chunk);       // sparse hole
            } else {
                r = read_block(fs, disk, tmp);
                if (r == 0) memcpy(dst + done, tmp + inblk, chunk);
            }
        }
        if (r) {
            kfree(tmp);
            return done ? (int)done : r;            // partial read beats an error
        }
        done += chunk;
    }

    kfree(tmp);
    return (int)done;
}

static int ext2_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    struct ext2_node *en = n->priv;
    struct ext2_fs   *fs = en->fs;
    uint32_t bs = fs->block_size;

    if (n->type != VFS_FILE) return -EINVAL;
    if (len == 0) return 0;

    uint32_t end = off + len;
    if (end < off || end > 0x7FFFFFFFu) return -EFBIG;

    uint8_t *tmp = kmalloc(bs);
    if (!tmp) return -ENOMEM;

    const uint8_t *src = buf;
    uint32_t done = 0;
    int r = 0;

    while (done < len) {
        uint32_t pos   = off + done;
        uint32_t fb    = pos / bs;
        uint32_t inblk = pos % bs;
        uint32_t chunk = bs - inblk;
        if (chunk > len - done) chunk = len - done;

        uint32_t disk;
        int created;
        r = alloc_fileblock(fs, en, fb, &disk, &created);
        if (r) break;

        if (created)                    memset(tmp, 0, bs);     // a fresh block starts zeroed
        else if (chunk != bs) {
            r = read_block(fs, disk, tmp);                      // read-modify-write
            if (r) break;
        }

        memcpy(tmp + inblk, src + done, chunk);
        r = write_block(fs, disk, tmp);
        if (r) break;

        done += chunk;
    }
    kfree(tmp);

    if (done > 0) {
        if (off + done > en->inode.size) {
            en->inode.size = off + done;
            n->size = en->inode.size;
        }
        en->inode.mtime = now();

        int wr = write_inode(fs, n->ino, &en->inode);
        flush_meta(fs);
        if (wr && !r) r = wr;
    }

    return done ? (int)done : r;
}

static int ext2_truncate(struct vfs_node *n, uint32_t size) {
    struct ext2_node *en = n->priv;
    struct ext2_fs   *fs = en->fs;
    uint32_t bs = fs->block_size;

    if (n->type != VFS_FILE) return -EINVAL;

    if (size < n->size) {
        free_from(fs, en, (size + bs - 1) / bs);

        if (size % bs) {                            // zero the tail of the last block we keep
            uint32_t disk;
            if (bmap(fs, &en->inode, size / bs, &disk) == 0 && disk) {
                uint8_t *buf = kmalloc(bs);
                if (buf) {
                    if (read_block(fs, disk, buf) == 0) {
                        memset(buf + size % bs, 0, bs - size % bs);
                        write_block(fs, disk, buf);
                    }
                    kfree(buf);
                }
            }
        }
    }

    en->inode.size = size; // growing just makes a sparse file
    n->size = size;
    en->inode.mtime = en->inode.ctime = now();

    int r = write_inode(fs, n->ino, &en->inode);
    flush_meta(fs);
    return r;
}

static int ext2_setattr(struct vfs_node *n, int mode, int uid, int gid) {
    struct ext2_node *en = n->priv;

    if (mode >= 0) en->inode.mode = (uint16_t)((en->inode.mode & S_IFMT) | (mode & 07777));
    if (uid >= 0)  en->inode.uid  = (uint16_t)uid;
    if (gid >= 0)  en->inode.gid  = (uint16_t)gid;
    en->inode.ctime = now();

    int r = write_inode(en->fs, n->ino, &en->inode);
    if (r) return r;

    n->mode = en->inode.mode;
    n->uid  = en->inode.uid;
    n->gid  = en->inode.gid;
    return 0;
}

static int ext2_create(struct vfs_node *dir, const char *name, uint32_t type,
                       struct vfs_node **out) {
    struct ext2_node *dn = dir->priv;
    struct ext2_fs   *fs = dn->fs;
    uint32_t bs  = fs->block_size;
    size_t   len = strlen(name);

    if (dir->type != VFS_DIR) return -ENOTDIR;
    if (len == 0 || len > VFS_NAME_MAX) return -ENAMETOOLONG;
    if (type != VFS_FILE && type != VFS_DIR) return -EINVAL;

    uint32_t group = (dir->ino - 1) / fs->inodes_per_group;
    uint32_t ino, blk = 0;

    int r = alloc_inode(fs, group, type == VFS_DIR, &ino);
    if (r) return r;

    struct ext2_inode in;
    memset(&in, 0, sizeof(in));
    in.atime = in.ctime = in.mtime = now();

    if (type == VFS_DIR) {
        r = alloc_block(fs, group, &blk);
        if (r) { free_inode(fs, ino, 1); flush_meta(fs); return r; }

        uint8_t *buf = kcalloc(1, bs);
        if (!buf) {
            free_block(fs, blk);
            free_inode(fs, ino, 1);
            flush_meta(fs);
            return -ENOMEM;
        }

        struct ext2_dirent *d1 = (struct ext2_dirent *)buf;       // "."
        d1->inode     = ino;
        d1->rec_len   = 12;
        d1->name_len  = 1;
        d1->file_type = fs->has_filetype ? FT_DIR : 0;
        buf[8] = '.';

        struct ext2_dirent *d2 = (struct ext2_dirent *)(buf + 12); // ".." takes the rest
        d2->inode     = dir->ino;
        d2->rec_len   = (uint16_t)(bs - 12);
        d2->name_len  = 2;
        d2->file_type = fs->has_filetype ? FT_DIR : 0;
        buf[20] = '.';
        buf[21] = '.';

        r = write_block(fs, blk, buf);
        kfree(buf);
        if (r) {
            free_block(fs, blk);
            free_inode(fs, ino, 1);
            flush_meta(fs);
            return r;
        }

        in.mode     = S_IFDIR | 0755;
        in.links    = 2;
        in.size     = bs;
        in.blocks   = fs->sectors_per_block;
        in.block[0] = blk;
    } else {
        in.mode  = S_IFREG | 0644;
        in.links = 1;
    }

    r = write_inode(fs, ino, &in);
    if (!r) r = dir_add(fs, dn, name, ino, ft_of(type));
    if (r) {                                        // undo everything
        if (blk) free_block(fs, blk);
        free_inode(fs, ino, type == VFS_DIR);
        flush_meta(fs);
        return r;
    }

    if (type == VFS_DIR) dn->inode.links++;         // the new ".." points at us
    dn->inode.mtime = now();
    write_inode(fs, dir->ino, &dn->inode);
    flush_meta(fs);

    struct vfs_node *vn = make_node(fs, ino);
    if (!vn) return -EIO;
    *out = vn;
    return 0;
}

static int ext2_unlink(struct vfs_node *dir, const char *name) {
    struct ext2_node *dn = dir->priv;
    struct ext2_fs   *fs = dn->fs;
    struct find_ctx ctx = { name, strlen(name), 0 };

    int r = dir_walk(dn, find_cb, &ctx);
    if (r < 0) return r;
    if (r != 1) return -ENOENT;

    struct vfs_node *vn = make_node(fs, ctx.ino);
    if (!vn) return -EIO;
    if (vn->type == VFS_DIR) { vfs_node_put(vn); return -EISDIR; }

    uint32_t removed;
    r = dir_remove(fs, dn, name, &removed);
    if (r) { vfs_node_put(vn); return r; }

    struct ext2_node *en = vn->priv;
    if (en->inode.links > 0) en->inode.links--;
    en->inode.ctime = now();
    write_inode(fs, vn->ino, &en->inode);

    dn->inode.mtime = now();
    write_inode(fs, dir->ino, &dn->inode);

    vfs_node_put(vn);           // frees the file's storage now, or when the last opener closes it
    flush_meta(fs);
    return 0;
}

static int ext2_rmdir(struct vfs_node *dir, const char *name) {
    struct ext2_node *dn = dir->priv;
    struct ext2_fs   *fs = dn->fs;
    struct find_ctx ctx = { name, strlen(name), 0 };

    int r = dir_walk(dn, find_cb, &ctx);
    if (r < 0) return r;
    if (r != 1) return -ENOENT;

    struct vfs_node *vn = make_node(fs, ctx.ino);
    if (!vn) return -EIO;
    if (vn->type != VFS_DIR) { vfs_node_put(vn); return -ENOTDIR; }

    struct ext2_node *en = vn->priv;
    r = dir_walk(en, empty_cb, NULL);
    if (r != 0) {
        vfs_node_put(vn);
        return r > 0 ? -ENOTEMPTY : r;
    }

    uint32_t removed;
    r = dir_remove(fs, dn, name, &removed);
    if (r) { vfs_node_put(vn); return r; }

    en->inode.links = 0;                            // "." and the parent's entry are gone
    en->inode.ctime = now();
    write_inode(fs, vn->ino, &en->inode);

    if (dn->inode.links > 1) dn->inode.links--;     // its ".." no longer points at us
    dn->inode.mtime = now();
    write_inode(fs, dir->ino, &dn->inode);

    vfs_node_put(vn);                               // frees the blocks and the inode
    flush_meta(fs);
    return 0;
}

static int ext2_rename(struct vfs_node *odir, const char *oname,
                       struct vfs_node *ndir, const char *nname) {
    struct ext2_node *od = odir->priv, *nd = ndir->priv;
    struct ext2_fs *fs = od->fs;
    size_t nlen = strlen(nname);
    int r;

    if (nd->fs != fs) return -EXDEV;
    if (nlen == 0 || nlen > VFS_NAME_MAX) return -ENAMETOOLONG;

    struct find_ctx oldc = { oname, strlen(oname), 0 };
    r = dir_walk(od, find_cb, &oldc);
    if (r < 0) return r;
    if (r != 1) return -ENOENT;

    struct find_ctx newc = { nname, nlen, 0 };
    r = dir_walk(nd, find_cb, &newc);
    if (r < 0) return r;
    if (r == 1 && newc.ino == oldc.ino) return 0;           // renaming onto itself

    struct vfs_node *src = make_node(fs, oldc.ino);
    if (!src) return -EIO;

    if (r == 1) {                                           // replace an existing file
        struct vfs_node *dst = make_node(fs, newc.ino);
        int bad = !dst || dst->type == VFS_DIR || src->type == VFS_DIR;
        vfs_node_put(dst);
        if (bad) { vfs_node_put(src); return -EEXIST; }

        r = ext2_unlink(ndir, nname);
        if (r) { vfs_node_put(src); return r; }
    }

    r = dir_add(fs, nd, nname, oldc.ino, ft_of(src->type));
    if (r == 0) {
        uint32_t removed;
        r = dir_remove(fs, od, oname, &removed);
    }

    if (r == 0 && src->type == VFS_DIR && odir != ndir) {
        set_dotdot(fs, src->priv, ndir->ino);               // moved between parents
        if (od->inode.links > 1) od->inode.links--;
        nd->inode.links++;
    }

    if (r == 0) {
        od->inode.mtime = nd->inode.mtime = now();
        write_inode(fs, odir->ino, &od->inode);
        if (nd != od) write_inode(fs, ndir->ino, &nd->inode);
    }

    vfs_node_put(src);
    flush_meta(fs);
    return r;
}

static struct vfs_ops ext2_ops = {
    .read     = ext2_read,
    .write    = ext2_write,
    .readdir  = ext2_readdir,
    .finddir  = ext2_finddir,
    .create   = ext2_create,
    .release  = ext2_release,
    .setattr  = ext2_setattr,
    .unlink   = ext2_unlink,
    .rmdir    = ext2_rmdir,
    .rename   = ext2_rename,
    .truncate = ext2_truncate,
};

// ---------- mounting ----------

struct vfs_node *ext2_mount(int drive, uint32_t start_lba) {
    struct ext2_fs    *fs   = NULL;
    struct ext2_super *sb   = NULL;
    uint8_t           *sbuf = NULL;
    struct vfs_node   *root = NULL;

    if (!ata_get(drive)) {
        kprintf("ext2: no such drive %d\n", drive);
        return NULL;
    }

    sbuf = kmalloc(1024);
    if (!sbuf) return NULL;

    // The superblock always sits 1024 bytes from the start = sector 2
    if (ata_read(drive, start_lba + 2, 2, sbuf) != 0) {
        kprintf("ext2: cannot read superblock\n");
        goto fail;
    }
    sb = (struct ext2_super *)sbuf;

    if (sb->magic != EXT2_MAGIC) {
        kprintf("ext2: bad magic %04x\n", sb->magic);
        goto fail;
    }
    if (sb->log_block_size > 2) {
        kprintf("ext2: block sizes above 4 KiB are not supported\n");
        goto fail;
    }
    if (!sb->blocks_per_group || !sb->inodes_per_group || !sb->inodes_count) {
        kprintf("ext2: corrupt superblock\n");
        goto fail;
    }
    if (sb->rev_level >= 1 && (sb->feature_incompat & ~INCOMPAT_FILETYPE)) {
        kprintf("ext2: unsupported incompatible features %08x\n", sb->feature_incompat);
        goto fail;
    }
    if (sb->rev_level >= 1 && (sb->feature_ro_compat & ~0x7u))
        kprintf("ext2: warning: unknown read-only-compat features %08x\n", sb->feature_ro_compat);

    fs = kcalloc(1, sizeof(*fs));
    if (!fs) goto fail;

    fs->sb                = sb;
    fs->drive             = drive;
    fs->start_lba         = start_lba;
    fs->block_size        = 1024u << sb->log_block_size;
    fs->sectors_per_block = fs->block_size / ATA_SECTOR_SIZE;
    fs->blocks_count      = sb->blocks_count;
    fs->inodes_count      = sb->inodes_count;
    fs->blocks_per_group  = sb->blocks_per_group;
    fs->inodes_per_group  = sb->inodes_per_group;
    fs->first_data_block  = sb->first_data_block;
    fs->inode_size        = sb->rev_level >= 1 ? sb->inode_size : 128;
    fs->has_filetype      = sb->rev_level >= 1 && (sb->feature_incompat & INCOMPAT_FILETYPE);

    if (fs->inode_size < sizeof(struct ext2_inode) || fs->inode_size > fs->block_size) {
        kprintf("ext2: bad inode size %u\n", fs->inode_size);
        goto fail;
    }

    fs->groups = (sb->blocks_count - sb->first_data_block +
                  sb->blocks_per_group - 1) / sb->blocks_per_group;

    // Load the whole group descriptor table; it starts in the block after the superblock
    uint32_t gdt_bytes = fs->groups * sizeof(struct ext2_gd);
    fs->gdt_blocks = (gdt_bytes + fs->block_size - 1) / fs->block_size;

    fs->gdt = kmalloc(fs->gdt_blocks * fs->block_size);
    if (!fs->gdt) goto fail;

    for (uint32_t i = 0; i < fs->gdt_blocks; i++) {
        if (read_block(fs, fs->first_data_block + 1 + i,
                       (uint8_t *)fs->gdt + i * fs->block_size) != 0) {
            kprintf("ext2: cannot read group descriptors\n");
            goto fail;
        }
    }

    root = make_node(fs, EXT2_ROOT_INO);
    if (!root || root->type != VFS_DIR) {
        kprintf("ext2: cannot read root directory\n");
        if (root) { vfs_node_put(root); root = NULL; }
        goto fail;
    }

    kprintf("ext2: %u blocks of %u bytes, %u groups, %u-byte inodes (read/write)\n",
            fs->blocks_count, fs->block_size, fs->groups, fs->inode_size);
    return root;                                    /* fs and sbuf stay alive for the mount */

fail:
    if (fs) { kfree(fs->gdt); kfree(fs); }
    kfree(sbuf);
    return NULL;
}