#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include "pipe.h"
#include "vfs.h"
#include "kheap.h"
#include "proc.h"

#define PIPE_SIZE 4096

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint32_t head, tail, count;
    int      reader_open, writer_open;
    int      nodes;                     // live end nodes; the pipe is freed at 0
};

static int pipe_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    struct pipe *p = n->priv;
    int nonblock = vfs_nonblock;
    (void)off;

    if (len == 0) return 0;

    while (p->count == 0) {
        if (!p->writer_open) return 0;              // every writer is gone: end of file (and literacy)
        if (nonblock) return -EAGAIN;
        sched_sleep(P_SLEEP_PIPE);
        if (current->killed) return -EINTR;
    }

    uint8_t *dst = buf;
    uint32_t got = 0;
    while (got < len && p->count > 0) {
        dst[got++] = p->buf[p->tail];
        p->tail = (p->tail + 1) % PIPE_SIZE;
        p->count--;
    }

    sched_wake(P_SLEEP_PIPE);                       // a writer may be waiting for room
    return (int)got;
}

static int pipe_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    struct pipe *p = n->priv;
    const uint8_t *src = buf;
    int nonblock = vfs_nonblock;
    uint32_t done = 0;
    (void)off;

    while (done < len) {
        if (!p->reader_open) return done ? (int)done : -EPIPE;

        if (p->count == PIPE_SIZE) {                // full: let readers drain it
            if (nonblock) return done ? (int)done : -EAGAIN;
            sched_wake(P_SLEEP_PIPE);
            sched_sleep(P_SLEEP_PIPE);
            if (current->killed) return done ? (int)done : -EINTR;
            continue;
        }

        while (done < len && p->count < PIPE_SIZE) {
            p->buf[p->head] = src[done++];
            p->head = (p->head + 1) % PIPE_SIZE;
            p->count++;
        }
        sched_wake(P_SLEEP_PIPE);                   // wake readers, because readers cant read while they sleep.
    }
    return (int)done;
}

static int reader_poll(struct vfs_node *n) {
    struct pipe *p = n->priv;
    return (p->count > 0 ? POLLIN : 0) | (!p->writer_open ? POLLHUP : 0);
}

static int writer_poll(struct vfs_node *n) {
    struct pipe *p = n->priv;
    return (p->count < PIPE_SIZE ? POLLOUT : 0) | (!p->reader_open ? POLLERR : 0);
}

static void pipe_drop(struct vfs_node *n, int is_reader) {
    struct pipe *p = n->priv;

    if (is_reader) p->reader_open = 0;
    else           p->writer_open = 0;
    sched_wake(P_SLEEP_PIPE);                       // sleepers re-check and see EOF / EPIPE

    kfree(n);
    if (--p->nodes == 0) kfree(p);
}

static void release_reader(struct vfs_node *n) { pipe_drop(n, 1); }
static void release_writer(struct vfs_node *n) { pipe_drop(n, 0); }

static struct vfs_ops reader_ops = {
    .read = pipe_read, .poll = reader_poll, .release = release_reader
};
static struct vfs_ops writer_ops = {
    .write = pipe_write, .poll = writer_poll, .release = release_writer
};

int pipe_create(struct vfs_file **rd, struct vfs_file **wr) {
    struct pipe     *p  = kcalloc(1, sizeof(*p));
    struct vfs_node *rn = kcalloc(1, sizeof(*rn));
    struct vfs_node *wn = kcalloc(1, sizeof(*wn));

    if (!p || !rn || !wn) {
        kfree(p);
        kfree(rn);
        kfree(wn);
        return -ENOMEM;
    }

    p->reader_open = 1;
    p->writer_open = 1;
    p->nodes       = 2;

    rn->type = VFS_CHAR;  rn->refs = 1;  rn->mode = 0010000 | 0600;   // a FIFO
    rn->ops  = &reader_ops;  rn->priv = p;
    wn->type = VFS_CHAR;  wn->refs = 1;  wn->mode = 0010000 | 0600;
    wn->ops  = &writer_ops;  wn->priv = p;

    struct vfs_file *r = vfs_file_alloc(rn, O_RDONLY);
    struct vfs_file *w = vfs_file_alloc(wn, O_WRONLY);

    vfs_node_put(rn);                               // the open files hold the references now
    vfs_node_put(wn);

    if (!r || !w) {
        vfs_file_put(r);
        vfs_file_put(w);
        return -ENOMEM;
    }

    *rd = r;
    *wr = w;
    return 0;
}