#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "blkdev.h"
#include "ata.h"
#include "ramfs.h"
#include "vfs.h"
#include "kheap.h"
#include "kprintf.h"
#include "paging.h"

#ifndef S_IFBLK
#define S_IFBLK 0060000
#endif

#define SECTOR       ATA_SECTOR_SIZE
#define MAX_LOGICAL  16

// A disk or a partition: a range of sectors on one ATA drive
struct blk {
    int      disk;          // ATA drive index
    int      part;          // 0 = the whole disk
    uint32_t start;         // first sector on the drive
    uint32_t sectors;       // length in sectors
};

#define BLKGETSIZE 0x1260
#define BLKGETINFO 0x12F0

struct blkinfo {
    uint32_t sectors;       // size in 512-byte sectors
    uint32_t start;         // first sector on the disk (0 for a whole disk)
    int32_t  disk;          // disk number
    int32_t  part;          // partition number, 0 for the whole disk
    uint32_t flags;         // 1 = removable
    char     bus[8];        // "ide", "sata", "usb"
    char     model[41];
};

static uint32_t next_ino = 0x8000;

// ---------- byte-granular I/O on top of whole sectors ---------- //

static int blk_io(struct blk *b, uint32_t off, uint32_t len, void *buf, int is_write) {
    uint8_t *p = buf;
    uint8_t sec[SECTOR];                            // for the parts of a sector
    uint64_t size = (uint64_t)b->sectors * SECTOR;
    uint32_t done = 0;

    if (size > 0x7FFFFFFFu) size = 0x7FFFFFFFu;     // byte offsets are 31 bits wide here
    if (len == 0) return 0;
    if (off >= size) return is_write ? -ENOSPC : 0; // reading past the end is end of file
    if ((uint64_t)off + len > size) len = (uint32_t)(size - off);

    while (done < len) {
        uint32_t pos  = off + done;
        uint32_t lba  = b->start + pos / SECTOR;
        uint32_t in   = pos % SECTOR;
        uint32_t left = len - done;
        int r;

        if (in == 0 && left >= SECTOR) {            // whole sectors: straight to the caller
            uint32_t n = left / SECTOR;
            r = is_write ? ata_write(b->disk, lba, n, p + done)
                         : ata_read(b->disk, lba, n, p + done);
            if (r) return done ? (int)done : -EIO;
            done += n * SECTOR;
        } else {                                    // a piece of one sector
            uint32_t chunk = SECTOR - in;
            if (chunk > left) chunk = left;

            r = ata_read(b->disk, lba, 1, sec);
            if (r == 0 && is_write) {
                memcpy(sec + in, p + done, chunk);
                r = ata_write(b->disk, lba, 1, sec);
            } else if (r == 0) {
                memcpy(p + done, sec + in, chunk);
            }
            if (r) return done ? (int)done : -EIO;
            done += chunk;
        }
    }
    return (int)done;
}

static int blk_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    return blk_io(n->priv, off, len, buf, 0);
}

static int blk_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    return blk_io(n->priv, off, len, (void *)buf, 1);
}

static int blk_ioctl(struct vfs_node *n, uint32_t req, uint32_t arg) {
    struct blk *b = n->priv;

    if (req == BLKGETINFO) {
        struct blkinfo *u = (struct blkinfo *)(uintptr_t)arg;
        const struct ata_drive *d = ata_get(b->disk);
        const char *bus = ata_bus_name(b->disk);

        if (!paging_check_user((uint32_t)(uintptr_t)u, sizeof(*u), 1)) return -EFAULT;
        memset(u, 0, sizeof(*u));
        u->sectors = b->sectors;
        u->start   = b->start;
        u->disk    = b->disk;
        u->part    = b->part;
        u->flags   = ata_is_removable(b->disk) ? 1u : 0u;
        for (int i = 0; i < 7 && bus[i]; i++) u->bus[i] = bus[i];
        if (d) memcpy(u->model, d->model, sizeof(u->model));
        return 0;
    }
    if (req == BLKGETSIZE) {                                // size in sectors, like Linux
        uint32_t *p = (uint32_t *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)p, sizeof(*p), 1)) return -EFAULT;
        *p = b->sectors;
        return 0;
    }
    return -ENOTTY;
}

static struct vfs_ops blk_ops = { .read = blk_read, .write = blk_write, .ioctl = blk_ioctl };

// ---------- creating the nodes ---------- //

static char *put_num(char *d, uint32_t v) {
    char t[11];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    return d;
}

// "disk<disk>" for part 0, otherwise "disk<disk>p<part>"
static char *make_name(int disk, int part) {
    char tmp[24];
    char *d = tmp;

    memcpy(d, "disk", 4);
    d += 4;
    d = put_num(d, (uint32_t)disk);
    if (part > 0) {
        *d++ = 'p';
        d = put_num(d, (uint32_t)part);
    }
    *d = '\0';

    size_t len = (size_t)(d - tmp) + 1;
    char *name = kcalloc(1, len);                   // kept for good: the name belongs to devfs
    if (name) memcpy(name, tmp, len);
    return name;
}

static void add_node(struct vfs_node *root, int disk, int part, uint32_t start, uint32_t sectors) {
    char *name = make_name(disk, part);
    struct blk *b = kcalloc(1, sizeof(*b));
    struct vfs_node *n = kcalloc(1, sizeof(*n));

    if (!name || !b || !n) {
        kfree(name);
        kfree(b);
        kfree(n);
        return;
    }

    b->disk    = disk;
    b->part    = part;
    b->start   = start;
    b->sectors = sectors;

    n->ino  = next_ino++;
    n->type = VFS_CHAR;
    n->refs = 1;                                    // owned by devfs, never freed
    n->size = sectors >= 0x7FFFFFFFu / SECTOR ? 0x7FFFFFFFu : sectors * SECTOR;
    n->mode = S_IFBLK | 0660;
    n->ops  = &blk_ops;
    n->priv = b;

    ramfs_add_node(root, name, n);
}

// ---------- partition tables ---------- //

static int is_extended(uint8_t type) {
    return type == 0x05 || type == 0x0F || type == 0x85;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void scan_disk(struct vfs_node *root, int disk) {
    const struct ata_drive *d = ata_get(disk);
    uint8_t sec[SECTOR];
    uint32_t ext_base = 0;

    if (!d) return;

    add_node(root, disk, 0, 0, d->sectors);         // the whole disk

    if (ata_read(disk, 0, 1, sec) != 0) return;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return;   // no partition table

    for (int i = 0; i < 4; i++) {                   // the four primary slots
        const uint8_t *e = sec + 446 + i * 16;
        uint8_t type  = e[4];
        uint32_t lba  = le32(e + 8);
        uint32_t cnt  = le32(e + 12);

        if (type == 0 || cnt == 0) continue;
        if (type == 0xEE) continue;                 // protective MBR of a GPT disk: not supported
        if (lba >= d->sectors || cnt > d->sectors - lba) continue;   // points outside the disk

        if (is_extended(type)) {
            if (!ext_base) ext_base = lba;
            continue;
        }
        add_node(root, disk, i + 1, lba, cnt);
        kprintf("disk%dp%d: start %u, %u sectors (%u MiB)\n", disk, i + 1, lba, cnt, cnt / 2048);
    }

    if (!ext_base) return;

    // Logical partitions: a chain of EBRs inside the extended partition. In each EBR the
    // first entry is a partition (relative to that EBR), the second links to the next EBR
    // (relative to the start of the extended partition).
    uint32_t ebr = ext_base;
    int logical = 5;

    for (int step = 0; step < MAX_LOGICAL; step++) {
        if (ata_read(disk, ebr, 1, sec) != 0) break;
        if (sec[510] != 0x55 || sec[511] != 0xAA) break;

        const uint8_t *e1 = sec + 446;
        uint32_t lba = ebr + le32(e1 + 8);
        uint32_t cnt = le32(e1 + 12);
        if (e1[4] != 0 && cnt != 0 && lba < d->sectors && cnt <= d->sectors - lba) {
            add_node(root, disk, logical, lba, cnt);
            kprintf("disk%dp%d: start %u, %u sectors (%u MiB)\n", disk, logical, lba, cnt, cnt / 2048);
            logical++;
        }

        const uint8_t *e2 = sec + 446 + 16;
        uint32_t next = le32(e2 + 8);
        if (!is_extended(e2[4]) || next == 0) break;
        ebr = ext_base + next;
    }
}

void blkdev_register(struct vfs_node *devroot) {
    for (int i = 0; i < ATA_MAX_DRIVES; i++)
        scan_disk(devroot, i);
}