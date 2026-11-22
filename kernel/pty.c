#define __KERNEL__
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "pty.h"
#include "vfs.h"
#include "kheap.h"
#include "proc.h"
#include "paging.h"
#include "pit.h"

// values missing from your headers get the Linux numbers
#ifndef TIOCGPTN
#define TIOCGPTN   0x80045430
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK 0x40045431
#endif
#ifndef TIOCSWINSZ
#define TIOCSWINSZ 0x5414
#endif
#ifndef TIOCSCTTY
#define TIOCSCTTY  0x540E
#endif
#ifndef FIONREAD
#define FIONREAD   0x541B
#endif
#ifndef INLCR
#define INLCR  0000100
#endif
#ifndef IGNCR
#define IGNCR  0000200
#endif
#ifndef ECHONL
#define ECHONL 0000100
#endif

#define PTY_MAX 16
#define PTY_BUF 4096

struct pty {
    int              index;
    struct vfs_node  master, slave;      // embedded: they live exactly as long as the pty
    int              master_open;        // 0 once the master side is closed (hangup)
    int              slave_seen;         // the slave has been opened at least once
    int              locked;             // TIOCSPTLCK: slave cannot be opened yet
    struct termios   t;
    struct winsize   ws;
    int              fg_pgrp;            // receives ^C / ^\ (TIOCSPGRP / TIOCSCTTY)

    // master -> slave: what the slave reads, after line editing
    uint8_t          in[PTY_BUF];
    uint32_t         in_len;             // all queued bytes (the unfinished line is at the end)
    uint32_t         in_ready;           // of those, how many a read() may take now
    uint32_t         eof_marks;          // ^D typed on an empty line

    // slave -> master: what the master reads (output + echo)
    uint8_t          out[PTY_BUF];
    uint32_t         out_head, out_tail, out_count;
};

static struct pty *ptys[PTY_MAX];

// ---------- output queue ---------- //

static void out_put(struct pty *p, uint8_t c) {
    if (p->out_count == PTY_BUF) return;            // full: drop (only used for echo)
    p->out[p->out_head] = c;
    p->out_head = (p->out_head + 1) % PTY_BUF;
    p->out_count++;
}

// output with the slave's output processing: \n -> \r\n
static void out_post(struct pty *p, uint8_t c) {
    if ((p->t.c_oflag & OPOST) && (p->t.c_oflag & ONLCR) && c == '\n') out_put(p, '\r');
    out_put(p, c);
}

static void echo_char(struct pty *p, uint8_t c) {
    if (c == '\n') {
        if ((p->t.c_lflag & ECHO) || (p->t.c_lflag & ECHONL)) out_post(p, '\n');
        return;
    }
    if (!(p->t.c_lflag & ECHO)) return;
    if (c == 127)                   { out_put(p, '^'); out_put(p, '?'); }
    else if (c < 32 && c != '\t')   { out_put(p, '^'); out_put(p, (uint8_t)(c + 64)); }
    else                            out_put(p, c);
}

static void echo_erase(struct pty *p) {
    if (!(p->t.c_lflag & ECHO)) return;
    out_put(p, '\b'); out_put(p, ' '); out_put(p, '\b');
}

// ---------- input side (master write -> slave read) ---------- //

static void pty_input_byte(struct pty *p, uint8_t c) {
    uint32_t l = p->t.c_lflag;
    uint32_t i = p->t.c_iflag;

    if (c == '\r') {
        if (i & IGNCR) return;
        if (i & ICRNL) c = '\n';
    } else if (c == '\n' && (i & INLCR)) {
        c = '\r';
    }

    if (l & ISIG) {
        int sig = 0;
        if (c == p->t.c_cc[VINTR])      sig = 2;
        else if (c == p->t.c_cc[VQUIT]) sig = 3;
        if (sig) {
            echo_char(p, c);
            p->in_len = p->in_ready = 0;            // flush pending input
            p->eof_marks = 0;
            if (p->fg_pgrp <= 0 || proc_kill_group(p->fg_pgrp, sig) == 0) {
                // nobody to interrupt: the shell is waiting at its prompt, so hand it an
                // empty line (the way the console does) and it prints a fresh prompt
                p->in[0] = '\n';
                p->in_len = p->in_ready = 1;
                if (l & ECHO) out_post(p, '\n');
            }
            return;
        }
    }

    if (l & ICANON) {
        // ^H as well as DEL: nxterm sends Backspace as 8
        if (c == p->t.c_cc[VERASE] || c == 0x7F || c == 0x08) {
            if (p->in_len > p->in_ready) { p->in_len--; echo_erase(p); }
            return;
        }
        if (c == p->t.c_cc[VKILL]) {
            while (p->in_len > p->in_ready) { p->in_len--; echo_erase(p); }
            return;
        }
        if (c == p->t.c_cc[VEOF]) {
            if (p->in_len == p->in_ready) p->eof_marks++;   // nothing pending: end of file
            else                          p->in_ready = p->in_len;
            return;
        }
        if (c == '\n') {
            if (p->in_len < PTY_BUF) p->in[p->in_len++] = c;
            p->in_ready = p->in_len;
            echo_char(p, c);
            return;
        }
        if (p->in_len >= PTY_BUF - 1) return;       // line too long: keep one slot for the \n
        p->in[p->in_len++] = c;
        echo_char(p, c);
    } else {
        if (p->in_len >= PTY_BUF) return;
        p->in[p->in_len++] = c;
        p->in_ready = p->in_len;
        echo_char(p, c);
    }
}

static int slave_gone(struct pty *p) {
    return p->slave_seen && p->slave.refs == 0;
}

static int master_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    struct pty *p = n->priv;
    const uint8_t *src = buf;
    int nonblock = vfs_nonblock;
    uint32_t done = 0;
    (void)off;

    while (done < len) {
        if (p->in_len >= PTY_BUF) {                 // slave is not reading: wait for room
            sched_wake(P_SLEEP_PIPE);
            if (slave_gone(p)) return done ? (int)done : -EIO;
            if (nonblock) return done ? (int)done : -EAGAIN;
            sched_sleep(P_SLEEP_PIPE);
            if (current->killed) return done ? (int)done : -EINTR;
            continue;
        }
        pty_input_byte(p, src[done++]);
    }
    sched_wake(P_SLEEP_PIPE);
    return (int)done;
}

static int slave_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    struct pty *p = n->priv;
    int nonblock = vfs_nonblock;
    uint32_t deadline = 0;
    int timed = 0;
    int canon;
    (void)off;

    if (len == 0) return 0;

    for (;;) {
        canon = (p->t.c_lflag & ICANON) != 0;
        if (canon) {
            if (p->in_ready > 0) break;
            if (p->eof_marks > 0) { p->eof_marks--; return 0; }
        } else if (p->in_len > 0) {
            break;
        }
        if (!p->master_open) return 0;              // hangup: end of file
        if (nonblock) return -EAGAIN;

        if (!canon && p->t.c_cc[VMIN] == 0) {       // poll-style read: wait at most VTIME/10 s
            if (p->t.c_cc[VTIME] == 0) return 0;
            if (!timed) {
                deadline = pit_ticks() + (uint32_t)p->t.c_cc[VTIME] * pit_freq() / 10;
                timed = 1;
            }
            if ((int32_t)(pit_ticks() - deadline) >= 0) return 0;
            if (proc_sleep(1) < 0) return -EINTR;
            continue;
        }
        sched_sleep(P_SLEEP_PIPE);
        if (current->killed) return -EINTR;
    }

    uint32_t avail = p->in_len;
    if (canon) {                                    // one line per read
        avail = p->in_ready;
        for (uint32_t k = 0; k < avail; k++) {
            if (p->in[k] == '\n') { avail = k + 1; break; }
        }
    }
    uint32_t cnt = avail < len ? avail : len;

    memcpy(buf, p->in, cnt);
    memmove(p->in, p->in + cnt, p->in_len - cnt);
    p->in_len -= cnt;
    p->in_ready = (p->in_ready >= cnt) ? p->in_ready - cnt : 0;

    sched_wake(P_SLEEP_PIPE);                       // the master may be waiting for room
    return (int)cnt;
}

// ---------- output side (slave write -> master read) ---------- //

static int slave_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    struct pty *p = n->priv;
    const uint8_t *src = buf;
    int nonblock = vfs_nonblock;
    uint32_t done = 0;
    (void)off;

    while (done < len) {
        if (!p->master_open) return done ? (int)done : -EIO;

        uint8_t c = src[done];
        uint32_t need = ((p->t.c_oflag & OPOST) && (p->t.c_oflag & ONLCR) && c == '\n') ? 2 : 1;
        if (PTY_BUF - p->out_count < need) {        // full: let the master drain it
            sched_wake(P_SLEEP_PIPE);
            if (nonblock) return done ? (int)done : -EAGAIN;
            sched_sleep(P_SLEEP_PIPE);
            if (current->killed) return done ? (int)done : -EINTR;
            continue;
        }
        out_post(p, c);
        done++;
    }
    sched_wake(P_SLEEP_PIPE);
    return (int)done;
}

static int master_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    struct pty *p = n->priv;
    int nonblock = vfs_nonblock;
    uint8_t *dst = buf;
    uint32_t got = 0;
    (void)off;

    if (len == 0) return 0;

    while (p->out_count == 0) {
        if (slave_gone(p)) return -EIO;             // like Linux: the slave side hung up
        if (nonblock) return -EAGAIN;
        sched_sleep(P_SLEEP_PIPE);
        if (current->killed) return -EINTR;
    }
    while (got < len && p->out_count > 0) {
        dst[got++] = p->out[p->out_tail];
        p->out_tail = (p->out_tail + 1) % PTY_BUF;
        p->out_count--;
    }
    sched_wake(P_SLEEP_PIPE);
    return (int)got;
}

// ---------- poll / ioctl / release ---------- //

static int master_poll(struct vfs_node *n) {
    struct pty *p = n->priv;
    int ev = 0;
    if (p->out_count > 0)    ev |= POLLIN;
    if (p->in_len < PTY_BUF) ev |= POLLOUT;
    if (slave_gone(p))       ev |= POLLHUP;
    return ev;
}

static int slave_poll(struct vfs_node *n) {
    struct pty *p = n->priv;
    int ev = 0;
    if (p->t.c_lflag & ICANON) {
        if (p->in_ready > 0 || p->eof_marks > 0) ev |= POLLIN;
    } else if (p->in_len > 0) {
        ev |= POLLIN;
    }
    if (p->out_count < PTY_BUF) ev |= POLLOUT;
    if (!p->master_open)        ev |= POLLHUP;
    return ev;
}

static int pty_ioctl(struct vfs_node *n, uint32_t req, uint32_t arg) {
    struct pty *p = n->priv;
    int is_master = (n == &p->master);

    switch (req) {
    case TIOCGPTN: {
        int *v = (int *)(uintptr_t)arg;
        if (!is_master) return -ENOTTY;
        if (!paging_check_user((uint32_t)(uintptr_t)v, sizeof(int), 1)) return -EFAULT;
        *v = p->index;
        return 0;
    }
    case TIOCSPTLCK: {
        const int *v = (const int *)(uintptr_t)arg;
        if (!is_master) return -ENOTTY;
        if (!paging_check_user((uint32_t)(uintptr_t)v, sizeof(int), 0)) return -EFAULT;
        p->locked = (*v != 0);
        return 0;
    }
    case TIOCGWINSZ: {
        struct winsize *w = (struct winsize *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)w, sizeof(*w), 1)) return -EFAULT;
        *w = p->ws;
        return 0;
    }
    case TIOCSWINSZ: {                              // no SIGWINCH: every signal kills here
        const struct winsize *w = (const struct winsize *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)w, sizeof(*w), 0)) return -EFAULT;
        p->ws = *w;
        return 0;
    }
    case TCGETS: {
        struct termios *t = (struct termios *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)t, sizeof(*t), 1)) return -EFAULT;
        *t = p->t;
        return 0;
    }
    case TCSETS:
    case TCSETSW:
    case TCSETSF: {
        const struct termios *t = (const struct termios *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)t, sizeof(*t), 0)) return -EFAULT;
        int was_canon = (p->t.c_lflag & ICANON) != 0;
        p->t = *t;
        if (req == TCSETSF) {                       // flush unread input
            p->in_len = p->in_ready = 0;
            p->eof_marks = 0;
        } else if (was_canon && !(p->t.c_lflag & ICANON)) {
            p->in_ready = p->in_len;                // leaving line mode: release the pending line
        }
        sched_wake(P_SLEEP_PIPE);
        return 0;
    }

    // The console's two helper ioctls (fush uses them before every prompt to get the
    // terminal back into a sane state). The argument is a value, not a pointer.
    case TIOCSRAW: {                                // 1 = raw, 0 = cooked with echo and ^C
        int was_canon = (p->t.c_lflag & ICANON) != 0;
        if (arg) p->t.c_lflag &= ~(unsigned)(ICANON | ECHO | ISIG);
        else     p->t.c_lflag |= (unsigned)(ICANON | ECHO | ISIG);
        p->t.c_cc[VMIN]  = 1;
        p->t.c_cc[VTIME] = 0;
        if (was_canon && arg) {
            p->in_ready = p->in_len;                // leaving line mode: release what is typed
        } else if (!was_canon && !arg) {
            p->in_len = p->in_ready = 0;            // entering line mode: start clean
            p->eof_marks = 0;
        }
        sched_wake(P_SLEEP_PIPE);
        return 0;
    }
    case TIOCSECHO:
        if (arg) p->t.c_lflag |= (unsigned)ECHO;
        else     p->t.c_lflag &= ~(unsigned)ECHO;
        return 0;

    case TIOCGPGRP: {
        int *v = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)v, sizeof(int), 1)) return -EFAULT;
        *v = p->fg_pgrp;
        return 0;
    }
    case TIOCSPGRP: {
        const int *v = (const int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)v, sizeof(int), 0)) return -EFAULT;
        if (*v < 0) return -EINVAL;
        p->fg_pgrp = *v;
        return 0;
    }
    case TIOCSCTTY:                                 // "this terminal is my foreground job's"
        if (!is_master) p->fg_pgrp = current->pgid;
        return 0;
    case FIONREAD: {
        int *v = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)v, sizeof(int), 1)) return -EFAULT;
        *v = is_master ? (int)p->out_count
                       : (int)((p->t.c_lflag & ICANON) ? p->in_ready : p->in_len);
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

// The pty is freed when the master is closed AND no one has the slave open.
static void master_release(struct vfs_node *n) {
    struct pty *p = n->priv;
    p->master_open = 0;
    if (ptys[p->index] == p) ptys[p->index] = NULL;
    sched_wake(P_SLEEP_PIPE);                       // slave readers see the hangup
    if (p->slave.refs == 0) kfree(p);
}

static void slave_release(struct vfs_node *n) {     // runs each time the last slave file closes
    struct pty *p = n->priv;
    sched_wake(P_SLEEP_PIPE);                       // master readers see EIO
    if (!p->master_open) kfree(p);
}

static int slave_open(struct vfs_node *n, struct vfs_node **out) {
    struct pty *p = n->priv;
    (void)out;
    if (p->locked || !p->master_open) return -EIO;
    p->slave_seen = 1;
    return 0;
}

static struct vfs_ops master_ops = {
    .read = master_read, .write = master_write, .poll = master_poll,
    .ioctl = pty_ioctl, .release = master_release,
};

static struct vfs_ops slave_ops = {
    .read = slave_read, .write = slave_write, .poll = slave_poll,
    .ioctl = pty_ioctl, .release = slave_release, .open = slave_open,
};

// ---------- /dev/ptmx ---------- //

static int ptmx_open(struct vfs_node *n, struct vfs_node **out) {
    int idx = -1;
    (void)n;

    for (int i = 0; i < PTY_MAX; i++)
        if (!ptys[i]) { idx = i; break; }
    if (idx < 0) return -EAGAIN;

    struct pty *p = kcalloc(1, sizeof(*p));
    if (!p) return -ENOMEM;

    p->index       = idx;
    p->master_open = 1;
    p->locked      = 1;
    p->ws.ws_row   = 24;
    p->ws.ws_col   = 80;

    p->t.c_iflag = ICRNL | IXON;
    p->t.c_oflag = OPOST | ONLCR;
    p->t.c_cflag = B38400 | CS8 | CREAD | HUPCL;
    p->t.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    p->t.c_cc[VINTR]  = 3;
    p->t.c_cc[VQUIT]  = 28;
    p->t.c_cc[VERASE] = 127;
    p->t.c_cc[VKILL]  = 21;
    p->t.c_cc[VEOF]   = 4;
    p->t.c_cc[VMIN]   = 1;
    p->t.c_cc[VTIME]  = 0;
    p->t.c_cc[VSTART] = 17;
    p->t.c_cc[VSTOP]  = 19;
    p->t.c_cc[VSUSP]  = 26;

    p->master.ino  = 0x7000 + (uint32_t)idx * 2;
    p->master.type = VFS_CHAR;
    p->master.refs = 1;                             // the reference vfs_file_open takes over
    p->master.mode = S_IFCHR | 0666;
    p->master.ops  = &master_ops;
    p->master.priv = p;

    p->slave.ino  = 0x7001 + (uint32_t)idx * 2;
    p->slave.type = VFS_CHAR;
    p->slave.refs = 0;                              // counts only lookups and open files
    p->slave.mode = S_IFCHR | 0620;
    p->slave.uid  = (uint16_t)vfs_uid;
    p->slave.gid  = (uint16_t)vfs_gid;
    p->slave.ops  = &slave_ops;
    p->slave.priv = p;

    ptys[idx] = p;
    *out = &p->master;
    return 0;
}

static struct vfs_ops ptmx_ops = { .open = ptmx_open };

static struct vfs_node ptmx_node = {
    .ino = 0x6001, .type = VFS_CHAR, .refs = 1, .mode = S_IFCHR | 0666, .ops = &ptmx_ops,
};

struct vfs_node *pty_ptmx_node(void) {
    return &ptmx_node;
}

// ---------- /dev/pts ---------- //

static struct vfs_node *pts_finddir(struct vfs_node *dir, const char *name) {
    (void)dir;
    if (!*name) return NULL;

    uint32_t idx = 0;
    for (const char *c = name; *c; c++) {
        if (*c < '0' || *c > '9' || idx > PTY_MAX) return NULL;
        idx = idx * 10 + (uint32_t)(*c - '0');
    }
    if (idx >= PTY_MAX || !ptys[idx]) return NULL;

    ptys[idx]->slave.refs++;                        // the caller owns this reference
    return &ptys[idx]->slave;
}

static int pts_readdir(struct vfs_node *dir, uint32_t index, struct vfs_dirent *out) {
    uint32_t seen = 0;
    (void)dir;

    for (int i = 0; i < PTY_MAX; i++) {
        if (!ptys[i]) continue;
        if (seen++ != index) continue;

        char tmp[4];
        int len = 0;
        if (i >= 10) tmp[len++] = (char)('0' + i / 10);
        tmp[len++] = (char)('0' + i % 10);
        memcpy(out->name, tmp, (size_t)len);
        out->name[len] = '\0';
        out->ino  = ptys[i]->slave.ino;
        out->type = VFS_CHAR;
        return 1;
    }
    return 0;
}

static struct vfs_ops pts_ops = { .finddir = pts_finddir, .readdir = pts_readdir };

static struct vfs_node pts_node = {
    .ino = 0x6000, .type = VFS_DIR, .refs = 1, .mode = S_IFDIR | 0755, .ops = &pts_ops,
};

struct vfs_node *pty_pts_node(void) {
    return &pts_node;
}
