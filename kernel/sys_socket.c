#define __KERNEL__
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include "syscall.h"
#include "usock.h"
#include "vfs.h"
#include "kheap.h"
#include "proc.h"
#include "paging.h"

#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif
#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

#define SYS_SOCKETCALL 102

#define US_BUF        4096
#define US_BACKLOG    8
#define US_PATH       108
#define US_MAX_BOUND  32

enum us_state { US_NEW = 0, US_LISTEN, US_CONN };

// One end of a socket. All kernel code runs with interrupts off, so there is no locking.
// 'peer' is a weak pointer: whoever closes first clears the other side's pointer.
struct usock {
    int            state;
    struct usock  *peer;
    int            rd_shut, wr_shut;        // shutdown(SHUT_RD / SHUT_WR) done on this end
    char           path[US_PATH];           // own name ("" = unnamed)
    char           peer_path[US_PATH];
    uint32_t       head, tail, count;       // data waiting to be read BY this end
    uint8_t        buf[US_BUF];
    struct usock  *backlog[US_BACKLOG];     // listening: connected ends not yet accepted
    int            nbacklog, backlog_max;
};

static struct usock *bound_tbl[US_MAX_BOUND];
static uint32_t      next_ino = 0x5000;

// ---------- teardown ---------- //

static void sock_teardown(struct usock *s) {
    for (int i = 0; i < US_MAX_BOUND; i++)
        if (bound_tbl[i] == s) bound_tbl[i] = NULL;

    if (s->state == US_LISTEN) {
        for (int i = 0; i < s->nbacklog; i++) {
            struct usock *c = s->backlog[i];        // never accepted: nobody owns it
            if (c->peer) c->peer->peer = NULL;      // the client sees a closed connection
            kfree(c);
        }
        s->nbacklog = 0;
    }
    if (s->peer) s->peer->peer = NULL;

    kfree(s);
    sched_wake(P_SLEEP_PIPE);
}

// ---------- data transfer ---------- //

static int sock_recv_impl(struct usock *s, void *buf, uint32_t len, int nonblock, int peek) {
    uint8_t *dst = buf;
    uint32_t got = 0;

    if (s->state != US_CONN) return -ENOTCONN;
    if (len == 0) return 0;

    while (s->count == 0) {
        if (s->rd_shut || !s->peer || s->peer->wr_shut) return 0;     // end of file
        if (nonblock) return -EAGAIN;
        sched_sleep(P_SLEEP_PIPE);
        if (current->killed) return -EINTR;
    }

    uint32_t tail = s->tail, cnt = s->count;
    while (got < len && cnt > 0) {
        dst[got++] = s->buf[tail];
        tail = (tail + 1) % US_BUF;
        cnt--;
    }
    if (!peek) {
        s->tail  = tail;
        s->count = cnt;
        sched_wake(P_SLEEP_PIPE);                   // the writer may be waiting for room
    }
    return (int)got;
}

static int sock_send_impl(struct usock *s, const void *buf, uint32_t len, int nonblock) {
    const uint8_t *src = buf;
    uint32_t done = 0;

    if (s->state != US_CONN) return -ENOTCONN;

    while (done < len) {
        struct usock *p = s->peer;
        if (!p || s->wr_shut || p->rd_shut) return done ? (int)done : -EPIPE;

        if (p->count == US_BUF) {
            sched_wake(P_SLEEP_PIPE);
            if (nonblock) return done ? (int)done : -EAGAIN;
            sched_sleep(P_SLEEP_PIPE);
            if (current->killed) return done ? (int)done : -EINTR;
            continue;
        }
        while (done < len && p->count < US_BUF) {
            p->buf[p->head] = src[done++];
            p->head = (p->head + 1) % US_BUF;
            p->count++;
        }
        sched_wake(P_SLEEP_PIPE);
    }
    return (int)done;
}

// ---------- vfs glue ---------- //

static int sock_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    (void)off;
    return sock_recv_impl(n->priv, buf, len, vfs_nonblock, 0);
}

static int sock_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    (void)off;
    return sock_send_impl(n->priv, buf, len, vfs_nonblock);
}

static int sock_poll(struct vfs_node *n) {
    struct usock *s = n->priv;
    struct usock *p = s->peer;
    int ev = 0;

    switch (s->state) {
    case US_LISTEN:
        if (s->nbacklog) ev |= POLLIN;
        break;
    case US_CONN:
        if (s->count > 0 || s->rd_shut || !p || p->wr_shut) ev |= POLLIN;
        if (!p) ev |= POLLHUP | POLLOUT;
        else if (p->count < US_BUF && !s->wr_shut && !p->rd_shut) ev |= POLLOUT;
        break;
    default:
        ev |= POLLOUT;
        break;
    }
    return ev;
}

static int sock_ioctl(struct vfs_node *n, uint32_t req, uint32_t arg) {
    struct usock *s = n->priv;
    if (req == FIONREAD) {
        int *p = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)p, sizeof(int), 1)) return -EFAULT;
        *p = (int)s->count;
        return 0;
    }
    return -ENOTTY;
}

static void sock_release(struct vfs_node *n) {
    sock_teardown(n->priv);
    kfree(n);
}

static struct vfs_ops sock_ops = {
    .read = sock_read, .write = sock_write, .poll = sock_poll,
    .ioctl = sock_ioctl, .release = sock_release,
};

// Wraps a socket in a node + open file. On failure the socket is freed.
static struct vfs_file *sock_wrap(struct usock *s, int flags) {
    struct vfs_node *n = kcalloc(1, sizeof(*n));
    if (!n) { sock_teardown(s); return NULL; }

    n->ino  = next_ino++;
    n->type = VFS_CHAR;
    n->refs = 1;
    n->mode = S_IFSOCK | 0777;
    n->ops  = &sock_ops;
    n->priv = s;

    struct vfs_file *f = vfs_file_alloc(n, flags);
    vfs_node_put(n);            // the file holds the node now (on failure this frees both)
    return f;
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

static int get_sock(int fd, struct vfs_file **fp, struct usock **sp) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !current->fds[fd]) return -EBADF;
    struct vfs_file *f = current->fds[fd];
    if (f->node->ops != &sock_ops) return -ENOTSOCK;
    if (fp) *fp = f;
    *sp = f->node->priv;
    return 0;
}

static int user_r(const void *p, uint32_t n) {         // kernel will READ user memory
    return paging_check_user((uint32_t)(uintptr_t)p, n, 0);
}
static int user_w(const void *p, uint32_t n) {         // kernel will WRITE user memory
    return paging_check_user((uint32_t)(uintptr_t)p, n, 1);
}

// ---------- addresses ---------- //

// struct sockaddr_un from user space -> normalized absolute path (US_PATH bytes)
static int sockaddr_path(const void *uaddr, uint32_t len, char *out) {
    char tmp[US_PATH + 1];
    char abs[VFS_PATH_MAX];
    char norm[VFS_PATH_MAX];
    uint16_t fam;

    if (len < 3 || len > 2 + US_PATH) return -EINVAL;
    if (!user_r(uaddr, len)) return -EFAULT;

    memcpy(&fam, uaddr, 2);
    if (fam != AF_UNIX) return -EAFNOSUPPORT;

    memcpy(tmp, (const uint8_t *)uaddr + 2, len - 2);
    tmp[len - 2] = '\0';                    // sun_path need not be NUL-terminated
    if (tmp[0] == '\0') return -EINVAL;     // abstract sockets: not supported

    size_t tl = strlen(tmp);
    if (tmp[0] == '/') {
        memcpy(abs, tmp, tl + 1);
    } else {
        size_t cl = strlen(current->cwd);
        if (cl + 1 + tl >= VFS_PATH_MAX) return -ENAMETOOLONG;
        memcpy(abs, current->cwd, cl);
        abs[cl] = '/';
        memcpy(abs + cl + 1, tmp, tl + 1);
    }

    int r = vfs_normalize(abs, norm);
    if (r) return r;
    if (strlen(norm) >= US_PATH) return -ENAMETOOLONG;
    memcpy(out, norm, strlen(norm) + 1);
    return 0;
}

// writes a sockaddr_un and the length, like getsockname() does
static int put_addr(void *uaddr, uint32_t *ulenp, const char *path) {
    uint8_t tmp[2 + US_PATH + 1];

    if (!user_r(ulenp, sizeof(uint32_t))) return -EFAULT;
    uint32_t cap  = *ulenp;
    uint32_t plen = path[0] ? (uint32_t)strlen(path) + 1 : 0;
    uint32_t full = 2 + plen;
    uint32_t n    = cap < full ? cap : full;

    if (n) {
        if (!user_w(uaddr, n)) return -EFAULT;
        tmp[0] = (uint8_t)AF_UNIX;
        tmp[1] = 0;
        memcpy(tmp + 2, path, plen);
        memcpy(uaddr, tmp, n);
    }
    if (!user_w(ulenp, sizeof(uint32_t))) return -EFAULT;
    *ulenp = full;
    return 0;
}

static struct usock *find_bound(const char *path) {
    for (int i = 0; i < US_MAX_BOUND; i++)
        if (bound_tbl[i] && strcmp(bound_tbl[i]->path, path) == 0) return bound_tbl[i];
    return NULL;
}

// ---------- the calls ---------- //

static int do_socket(uint32_t domain, uint32_t type, uint32_t proto) {
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    if (type & ~(0xFu | SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
    if ((type & 0xF) != SOCK_STREAM) return -ESOCKTNOSUPPORT;
    if (proto != 0) return -EPROTONOSUPPORT;

    struct usock *s = kcalloc(1, sizeof(*s));
    if (!s) return -ENOMEM;

    struct vfs_file *f = sock_wrap(s, O_RDWR | ((type & SOCK_NONBLOCK) ? O_NONBLOCK : 0));
    if (!f) return -ENOMEM;

    int fd = install_fd(f);
    if (fd < 0) vfs_file_put(f);
    return fd;
}

static int do_socketpair(uint32_t domain, uint32_t type, uint32_t proto, int *usv) {
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    if (type & ~(0xFu | SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
    if ((type & 0xF) != SOCK_STREAM) return -ESOCKTNOSUPPORT;
    if (proto != 0) return -EPROTONOSUPPORT;
    if (!user_w(usv, 2 * sizeof(int))) return -EFAULT;

    struct usock *a = kcalloc(1, sizeof(*a));
    struct usock *b = kcalloc(1, sizeof(*b));
    if (!a || !b) { kfree(a); kfree(b); return -ENOMEM; }

    a->state = b->state = US_CONN;
    a->peer = b;
    b->peer = a;

    int fl = O_RDWR | ((type & SOCK_NONBLOCK) ? O_NONBLOCK : 0);
    struct vfs_file *fa = sock_wrap(a, fl);
    if (!fa) { sock_teardown(b); return -ENOMEM; }        // a is already gone
    struct vfs_file *fb = sock_wrap(b, fl);
    if (!fb) { vfs_file_put(fa); return -ENOMEM; }

    int fd0 = install_fd(fa);
    if (fd0 < 0) { vfs_file_put(fa); vfs_file_put(fb); return fd0; }
    int fd1 = install_fd(fb);
    if (fd1 < 0) {
        current->fds[fd0] = NULL;
        vfs_file_put(fa);
        vfs_file_put(fb);
        return fd1;
    }
    usv[0] = fd0;
    usv[1] = fd1;
    return 0;
}

// the directory the socket name would live in must exist and be writable
static int check_parent(const char *path) {
    char parent[US_PATH];
    struct vfs_stat st;
    size_t slash = strlen(path);

    while (slash > 0 && path[slash] != '/') slash--;
    if (slash == 0) { parent[0] = '/'; parent[1] = '\0'; }
    else            { memcpy(parent, path, slash); parent[slash] = '\0'; }

    int r = vfs_stat(parent, &st);
    if (r) return r;
    if (st.type != VFS_DIR) return -ENOTDIR;
    return vfs_access_path(parent, ACC_W | ACC_X);
}

static int do_bind(int fd, const void *uaddr, uint32_t len) {
    struct usock *s;
    char path[US_PATH];
    struct vfs_stat st;

    int r = get_sock(fd, NULL, &s);
    if (r) return r;
    if (s->state != US_NEW || s->path[0]) return -EINVAL;

    r = sockaddr_path(uaddr, len, path);
    if (r) return r;

    if (find_bound(path) || vfs_stat(path, &st) == 0) return -EADDRINUSE;
    r = check_parent(path);
    if (r) return r;

    for (int i = 0; i < US_MAX_BOUND; i++) {
        if (!bound_tbl[i]) {
            memcpy(s->path, path, strlen(path) + 1);
            bound_tbl[i] = s;
            return 0;
        }
    }
    return -ENOMEM;
}

static int do_listen(int fd, int backlog) {
    struct usock *s;
    int r = get_sock(fd, NULL, &s);
    if (r) return r;
    if (!s->path[0] || s->state == US_CONN) return -EINVAL;

    if (backlog < 1) backlog = 1;
    if (backlog > US_BACKLOG) backlog = US_BACKLOG;
    s->state = US_LISTEN;
    s->backlog_max = backlog;
    return 0;
}

static int do_connect(int fd, const void *uaddr, uint32_t len) {
    struct vfs_file *f;
    struct usock *s;
    char path[US_PATH];

    int r = get_sock(fd, &f, &s);
    if (r) return r;
    if (s->state == US_CONN) return -EISCONN;
    if (s->state == US_LISTEN) return -EINVAL;

    r = sockaddr_path(uaddr, len, path);
    if (r) return r;

    struct usock *srv = find_bound(path);
    if (!srv) return -ENOENT;
    if (srv->state != US_LISTEN) return -ECONNREFUSED;

    struct usock *c = kcalloc(1, sizeof(*c));       // the server's end of this connection
    if (!c) return -ENOMEM;

    int nonblock = (f->flags & O_NONBLOCK) != 0;
    while (srv->nbacklog >= srv->backlog_max) {
        if (nonblock) { kfree(c); return -EAGAIN; }
        sched_sleep(P_SLEEP_PIPE);
        if (current->killed) { kfree(c); return -EINTR; }
        srv = find_bound(path);                     // it may have gone away meanwhile
        if (!srv || srv->state != US_LISTEN) { kfree(c); return -ECONNREFUSED; }
    }

    c->state = US_CONN;
    c->peer  = s;
    memcpy(c->path, srv->path, strlen(srv->path) + 1);
    memcpy(c->peer_path, s->path, strlen(s->path) + 1);
    memcpy(s->peer_path, srv->path, strlen(srv->path) + 1);
    s->peer  = c;
    s->state = US_CONN;

    srv->backlog[srv->nbacklog++] = c;
    sched_wake(P_SLEEP_PIPE);                       // accept() may be waiting
    return 0;
}

static int do_accept4(int fd, void *uaddr, uint32_t *ulenp, uint32_t flags) {
    struct vfs_file *f;
    struct usock *s;

    int r = get_sock(fd, &f, &s);
    if (r) return r;
    if (s->state != US_LISTEN) return -EINVAL;
    if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) return -EINVAL;
    if (uaddr && !ulenp) return -EINVAL;
    if (uaddr && (!user_r(ulenp, sizeof(uint32_t)))) return -EFAULT;

    int nonblock = (f->flags & O_NONBLOCK) != 0;
    while (s->nbacklog == 0) {
        if (nonblock) return -EAGAIN;
        sched_sleep(P_SLEEP_PIPE);
        if (current->killed) return -EINTR;
    }

    struct usock *c = s->backlog[0];
    for (int i = 1; i < s->nbacklog; i++) s->backlog[i - 1] = s->backlog[i];
    s->nbacklog--;
    sched_wake(P_SLEEP_PIPE);                       // a connect() may be waiting for room

    struct vfs_file *nf = sock_wrap(c, O_RDWR | ((flags & SOCK_NONBLOCK) ? O_NONBLOCK : 0));
    if (!nf) return -ENOMEM;

    int nfd = install_fd(nf);
    if (nfd < 0) { vfs_file_put(nf); return nfd; }

    if (uaddr) {
        // the client's own name if it had one, else an empty address
        struct usock *acc = nf->node->priv;
        put_addr(uaddr, ulenp, acc->peer_path);
    }
    return nfd;
}

static int do_getname(int fd, void *uaddr, uint32_t *ulenp, int peer) {
    struct usock *s;
    int r = get_sock(fd, NULL, &s);
    if (r) return r;
    if (!uaddr || !ulenp) return -EFAULT;
    if (peer && s->state != US_CONN) return -ENOTCONN;
    return put_addr(uaddr, ulenp, peer ? s->peer_path : s->path);
}

static int do_shutdown(int fd, int how) {
    struct usock *s;
    int r = get_sock(fd, NULL, &s);
    if (r) return r;
    if (how < SHUT_RD || how > SHUT_RDWR) return -EINVAL;
    if (s->state != US_CONN) return -ENOTCONN;

    if (how == SHUT_RD || how == SHUT_RDWR) s->rd_shut = 1;
    if (how == SHUT_WR || how == SHUT_RDWR) s->wr_shut = 1;
    sched_wake(P_SLEEP_PIPE);
    return 0;
}

static int do_sendto(int fd, const void *buf, uint32_t len, uint32_t flags) {
    struct vfs_file *f;
    struct usock *s;
    int r = get_sock(fd, &f, &s);
    if (r) return r;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;
    if (!user_r(buf, len)) return -EFAULT;

    int nonblock = ((f->flags & O_NONBLOCK) != 0) || (flags & MSG_DONTWAIT);
    return sock_send_impl(s, buf, len, nonblock);
}

static int do_recvfrom(int fd, void *buf, uint32_t len, uint32_t flags,
                       void *uaddr, uint32_t *ulenp) {
    struct vfs_file *f;
    struct usock *s;
    int r = get_sock(fd, &f, &s);
    if (r) return r;
    if (len == 0) return 0;
    if (len > 65536) len = 65536;
    if (!user_w(buf, len)) return -EFAULT;

    int nonblock = ((f->flags & O_NONBLOCK) != 0) || (flags & MSG_DONTWAIT);
    int n = sock_recv_impl(s, buf, len, nonblock, (flags & MSG_PEEK) != 0);
    if (n >= 0 && uaddr && ulenp && user_w(ulenp, sizeof(uint32_t)))
        *ulenp = 0;                                 // stream sockets: no source address
    return n;
}

static int do_setsockopt(int fd) {
    struct usock *s;
    return get_sock(fd, NULL, &s);                  // accepted and ignored
}

static int do_getsockopt(int fd, int level, int opt, void *uval, uint32_t *ulenp) {
    struct usock *s;
    int r = get_sock(fd, NULL, &s);
    if (r) return r;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (!uval || !ulenp || !user_w(uval, sizeof(int)) || !user_w(ulenp, sizeof(uint32_t)))
        return -EFAULT;

    int v;
    switch (opt) {
    case SO_ERROR:      v = 0; break;
    case SO_TYPE:       v = SOCK_STREAM; break;
    case SO_ACCEPTCONN: v = (s->state == US_LISTEN); break;
    case SO_SNDBUF:
    case SO_RCVBUF:     v = US_BUF; break;
    default:            return -ENOPROTOOPT;
    }
    *(int *)uval = v;
    *ulenp = sizeof(int);
    return 0;
}

// socketcall(call, args): args points to an array of user words (Linux i386 ABI)
static int sys_socketcall(struct registers *r) {
    static const uint8_t nargs[19] = {
        0, 3, 3, 3, 2, 3, 3, 3, 4, 4, 4, 6, 6, 2, 5, 5, 3, 3, 4
    };
    uint32_t call = r->ebx;
    uint32_t a[6] = {0, 0, 0, 0, 0, 0};

    if (call < 1 || call > 18) return -EINVAL;
    uint32_t n = nargs[call];
    if (!user_r((const void *)(uintptr_t)r->ecx, n * 4)) return -EFAULT;
    memcpy(a, (const void *)(uintptr_t)r->ecx, n * 4);

    switch (call) {
    case 1:  return do_socket(a[0], a[1], a[2]);
    case 2:  return do_bind((int)a[0], (const void *)(uintptr_t)a[1], a[2]);
    case 3:  return do_connect((int)a[0], (const void *)(uintptr_t)a[1], a[2]);
    case 4:  return do_listen((int)a[0], (int)a[1]);
    case 5:  return do_accept4((int)a[0], (void *)(uintptr_t)a[1], (uint32_t *)(uintptr_t)a[2], 0);
    case 6:  return do_getname((int)a[0], (void *)(uintptr_t)a[1], (uint32_t *)(uintptr_t)a[2], 0);
    case 7:  return do_getname((int)a[0], (void *)(uintptr_t)a[1], (uint32_t *)(uintptr_t)a[2], 1);
    case 8:  return do_socketpair(a[0], a[1], a[2], (int *)(uintptr_t)a[3]);
    case 9:  return do_sendto((int)a[0], (const void *)(uintptr_t)a[1], a[2], a[3]);
    case 10: return do_recvfrom((int)a[0], (void *)(uintptr_t)a[1], a[2], a[3], NULL, NULL);
    case 11: return do_sendto((int)a[0], (const void *)(uintptr_t)a[1], a[2], a[3]);
    case 12: return do_recvfrom((int)a[0], (void *)(uintptr_t)a[1], a[2], a[3],
                                (void *)(uintptr_t)a[4], (uint32_t *)(uintptr_t)a[5]);
    case 13: return do_shutdown((int)a[0], (int)a[1]);
    case 14: return do_setsockopt((int)a[0]);
    case 15: return do_getsockopt((int)a[0], (int)a[1], (int)a[2],
                                  (void *)(uintptr_t)a[3], (uint32_t *)(uintptr_t)a[4]);
    case 18: return do_accept4((int)a[0], (void *)(uintptr_t)a[1],
                               (uint32_t *)(uintptr_t)a[2], a[3]);
    default: return -EOPNOTSUPP;                    // sendmsg / recvmsg
    }
}

void sys_socket_init(void) {
    syscall_register(SYS_SOCKETCALL, sys_socketcall);
}