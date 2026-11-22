#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "devfs.h"
#include "ramfs.h"
#include "vfs.h"
#include "tty.h"
#include "pty.h"
#include "pit.h"
#include "fbdev.h"
#include "mouse.h"

// ---------- /dev/null, /dev/zero ---------- //

static int null_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    (void)n; (void)off; (void)len; (void)buf;
    return 0;                                   // always end-of-file
}

static int sink_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    (void)n; (void)off; (void)buf;
    return (int)len;                            // swallow everything
}

static int zero_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    (void)n; (void)off;
    memset(buf, 0, len);
    return (int)len;
}

// ---------- /dev/random, /dev/urandom ---------- //

// xorshift32: fine for games and tests, NOT cryptographically secure
static uint32_t rng_state = 0x2545F491u;

static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int random_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    (void)n; (void)off;
    uint8_t *p = buf;
    for (uint32_t i = 0; i < len; i++)
        p[i] = (uint8_t)(rng_next() >> 11);
    return (int)len;
}

static struct vfs_ops null_ops   = { .read = null_read,   .write = sink_write };
static struct vfs_ops zero_ops   = { .read = zero_read,   .write = sink_write };
static struct vfs_ops random_ops = { .read = random_read, .write = sink_write };

static struct vfs_node null_node = {
    .ino = 11, .type = VFS_CHAR, .refs = 1, .mode = S_IFCHR | 0666, .ops = &null_ops,
};
static struct vfs_node zero_node = {
    .ino = 12, .type = VFS_CHAR, .refs = 1, .mode = S_IFCHR | 0666, .ops = &zero_ops,
};
static struct vfs_node random_node = {
    .ino = 13, .type = VFS_CHAR, .refs = 1, .mode = S_IFCHR | 0666, .ops = &random_ops,
};

struct vfs_node *devfs_create(void) {
    struct vfs_node *root = ramfs_create_root();
    if (!root) return NULL;
    root->mode = S_IFDIR | 0755;                // only root may add device nodes

    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    rng_state ^= lo ^ (hi << 7) ^ (pit_ticks() << 3);
    if (!rng_state) rng_state = 1;

    ramfs_add_node(root, "null",    &null_node);
    ramfs_add_node(root, "zero",    &zero_node);
    ramfs_add_node(root, "random",  &random_node);
    ramfs_add_node(root, "urandom", &random_node);
    ramfs_add_node(root, "console", tty_console());
    ramfs_add_node(root, "tty",     tty_console());
    ramfs_add_node(root, "tty0",    tty_console());

    ramfs_add_node(root, "ptmx",    pty_ptmx_node());   // open() it to get a new pty master
    ramfs_add_node(root, "pts",     pty_pts_node());    // /dev/pts/N = the slave sides

    struct vfs_node *fb = fbdev_node();
    if (fb) ramfs_add_node(root, "fb0", fb);

    struct vfs_node *mouse = mouse_node();
    ramfs_add_node(root, "psaux", mouse);
    ramfs_add_node(root, "mouse", mouse);

    struct vfs_node *input;                     // /dev/input/mice
    if (root->ops->create(root, "input", VFS_DIR, &input) == 0) {
        ramfs_add_node(input, "mice", mouse);
        vfs_node_put(input);
    }
    return root;
}