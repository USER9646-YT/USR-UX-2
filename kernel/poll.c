#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include "syscall.h"
#include "proc.h"
#include "vfs.h"
#include "paging.h"
#include "pit.h"

#define MAX_POLL     64
#define FDSET_BYTES  128            // 1024 bits

// events that are ready on a descriptor, or POLLNVAL if it isn't open
static int file_events(int fd, int want) {
    struct vfs_file *f = (fd >= 0 && fd < VFS_MAX_FDS) ? current->fds[fd] : NULL;
    if (!f) return POLLNVAL;

    int ev = want & (POLLIN | POLLOUT);             // no poll operation: always ready
    if (f->node->ops && f->node->ops->poll)
        ev = f->node->ops->poll(f->node) & (want | POLLERR | POLLHUP);
    return ev;
}

static uint32_t to_ticks(uint32_t sec, uint32_t usec) {     // usec < 1000000
    uint32_t hz = pit_freq();
    if (sec > 100000u) sec = 100000u;
    return sec * hz + (usec * hz + 999999u) / 1000000u;
}

// poll(fds, nfds, timeout_ms): 168
static int sys_poll(struct registers *r) {
    struct pollfd *u = (struct pollfd *)(uintptr_t)r->ebx;
    uint32_t nfds = r->ecx;
    int timeout = (int)r->edx;
    struct pollfd p[MAX_POLL];

    if (nfds > MAX_POLL) return -EINVAL;
    if (nfds && !paging_check_user((uint32_t)(uintptr_t)u, nfds * sizeof(p[0]), 1)) return -EFAULT;
    memcpy(p, u, nfds * sizeof(p[0]));

    int has_deadline = timeout >= 0;
    uint32_t deadline = 0;
    if (has_deadline)
        deadline = pit_ticks() + to_ticks((uint32_t)timeout / 1000u,
                                          ((uint32_t)timeout % 1000u) * 1000u);

    int ready;
    for (;;) {
        ready = 0;
        for (uint32_t i = 0; i < nfds; i++) {
            if (p[i].fd < 0) { p[i].revents = 0; continue; }
            int ev = file_events(p[i].fd, p[i].events);
            p[i].revents = (short)ev;
            if (ev) ready++;
        }
        if (ready) break;
        if (has_deadline && (int32_t)(pit_ticks() - deadline) >= 0) break;
        if (proc_sleep(1) < 0) return -EINTR;
    }

    memcpy(u, p, nfds * sizeof(p[0]));
    return ready;
}

static int bit(const uint8_t *s, int fd)     { return (s[fd >> 3] >> (fd & 7)) & 1; }
static void setbit(uint8_t *s, int fd)       { s[fd >> 3] |= (uint8_t)(1u << (fd & 7)); }

static int copy_in(uint8_t *dst, uint32_t usrc, uint32_t n) {
    if (!usrc) return 0;
    if (!paging_check_user(usrc, n, 0)) return -EFAULT;
    memcpy(dst, (const void *)(uintptr_t)usrc, n);
    return 0;
}

static int copy_out(uint32_t udst, const uint8_t *src, uint32_t n) {
    if (!udst) return 0;
    if (!paging_check_user(udst, n, 1)) return -EFAULT;
    memcpy((void *)(uintptr_t)udst, src, n);
    return 0;
}

// _newselect(n, readfds, writefds, exceptfds, timeout): 142
static int sys_select(struct registers *r) {
    int n = (int)r->ebx;
    uint32_t ur = r->ecx, uw = r->edx, ue = r->esi, utv = r->edi;
    uint8_t rin[FDSET_BYTES], win[FDSET_BYTES], rout[FDSET_BYTES], wout[FDSET_BYTES];
    struct timeval tv;
    int err;

    if (n < 0 || n > 1024) return -EINVAL;
    uint32_t nbytes = ((uint32_t)n + 7) / 8;

    memset(rin, 0, sizeof(rin));
    memset(win, 0, sizeof(win));
    if ((err = copy_in(rin, ur, nbytes)) != 0) return err;
    if ((err = copy_in(win, uw, nbytes)) != 0) return err;

    int has_deadline = 0;
    uint32_t deadline = 0;
    if (utv) {
        if (!paging_check_user(utv, sizeof(tv), 0)) return -EFAULT;
        memcpy(&tv, (const void *)(uintptr_t)utv, sizeof(tv));
        if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000) return -EINVAL;
        has_deadline = 1;
        deadline = pit_ticks() + to_ticks((uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec);
    }

    int ready;
    for (;;) {
        ready = 0;
        memset(rout, 0, sizeof(rout));
        memset(wout, 0, sizeof(wout));

        for (int fd = 0; fd < n; fd++) {
            int wr = bit(rin, fd), ww = bit(win, fd);
            if (!wr && !ww) continue;

            int ev = file_events(fd, (wr ? POLLIN : 0) | (ww ? POLLOUT : 0));
            if (ev & POLLNVAL) return -EBADF;

            if (wr && (ev & (POLLIN | POLLHUP | POLLERR))) { setbit(rout, fd); ready++; }
            if (ww && (ev & (POLLOUT | POLLERR)))          { setbit(wout, fd); ready++; }
        }
        if (ready) break;
        if (has_deadline && (int32_t)(pit_ticks() - deadline) >= 0) break;
        if (proc_sleep(1) < 0) return -EINTR;
    }

    uint8_t zero[FDSET_BYTES];
    memset(zero, 0, sizeof(zero));
    if ((err = copy_out(ur, rout, nbytes)) != 0) return err;
    if ((err = copy_out(uw, wout, nbytes)) != 0) return err;
    if ((err = copy_out(ue, zero, nbytes)) != 0) return err;     // no exceptional conditions
    return ready;
}

void sys_poll_init(void) {
    syscall_register(168, sys_poll);
    syscall_register(142, sys_select);
}