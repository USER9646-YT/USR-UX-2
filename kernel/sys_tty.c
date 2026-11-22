#define __KERNEL__
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include "syscall.h"
#include "proc.h"
#include "vfs.h"
#include "paging.h"
#include "terminal.h"
#include "tty.h"

static int gfx_owner;                    // pid of the program that switched the console to graphics
static int kbd_mode = K_XLATE;           // what KDGKBMODE reports; the keyboard only ever delivers K_XLATE
static struct vt_mode vt_mode_saved;     // VT_GETMODE returns what VT_SETMODE stored (VT_AUTO at first)

void sys_tty_hook(void) {
    // the program that owned the screen died without giving it back: restore the text console
    if (gfx_owner && proc_signal(gfx_owner, 0) == -ESRCH) {
        gfx_owner = 0;
        terminal_set_graphics(0);
    }
}

void sys_tty_reset_console(void) {
    if (gfx_owner) {
        gfx_owner = 0;
        terminal_set_graphics(0);
    }
}

static int console_ioctl(uint32_t req, uint32_t arg) {
    switch (req) {
    case TIOCSECHO:
        tty_set_echo((int)arg);
        return 0;
    case TIOCSRAW:
        tty_set_raw((int)arg);
        return 0;

    case TIOCGWINSZ: {
        struct winsize *ws = (struct winsize *)(uintptr_t)arg;
        int rows, cols;
        if (!paging_check_user((uint32_t)(uintptr_t)ws, sizeof(*ws), 1)) return -EFAULT;
        terminal_get_size(&rows, &cols);
        ws->ws_row    = (unsigned short)rows;
        ws->ws_col    = (unsigned short)cols;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }

    case TIOCGPGRP: {
        int *p = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)p, sizeof(int), 1)) return -EFAULT;
        *p = tty_get_fg();
        return 0;
    }
    case TIOCSPGRP: {
        const int *p = (const int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)p, sizeof(int), 0)) return -EFAULT;
        if (*p < 0) return -EINVAL;
        tty_set_fg(*p);
        return 0;
    }

    case TCGETS: {
        struct termios *t = (struct termios *)(uintptr_t)arg;
        int canon, echo, isig, vmin, vtime;

        if (!paging_check_user((uint32_t)(uintptr_t)t, sizeof(*t), 1)) return -EFAULT;
        tty_get_flags(&canon, &echo, &isig, &vmin, &vtime);

        memset(t, 0, sizeof(*t));
        t->c_iflag = ICRNL | IXON;
        t->c_oflag = OPOST | ONLCR;
        t->c_cflag = B38400 | CS8 | CREAD | HUPCL;
        t->c_lflag = IEXTEN | ECHOE | ECHOK | (canon ? ICANON : 0) |
                     (echo ? ECHO : 0) | (isig ? ISIG : 0);
        t->c_cc[VINTR]  = 3;
        t->c_cc[VQUIT]  = 28;
        t->c_cc[VERASE] = 8;
        t->c_cc[VKILL]  = 21;
        t->c_cc[VEOF]   = 4;
        t->c_cc[VTIME]  = (unsigned char)vtime;
        t->c_cc[VMIN]   = (unsigned char)vmin;
        t->c_cc[VSTART] = 17;
        t->c_cc[VSTOP]  = 19;
        t->c_cc[VSUSP]  = 26;
        return 0;
    }
    case TCSETS:
    case TCSETSW:
    case TCSETSF: {
        const struct termios *t = (const struct termios *)(uintptr_t)arg;

        if (!paging_check_user((uint32_t)(uintptr_t)t, sizeof(*t), 0)) return -EFAULT;
        tty_configure((t->c_lflag & ICANON) != 0, (t->c_lflag & ECHO) != 0,
                      (t->c_lflag & ISIG) != 0, t->c_cc[VMIN], t->c_cc[VTIME]);
        return 0;
    }

    case KDSETMODE:
        if (arg == KD_GRAPHICS) {
            gfx_owner = current->pid;
            terminal_set_graphics(1);
        } else if (arg == KD_TEXT) {
            gfx_owner = 0;
            terminal_set_graphics(0);
        } else {
            return -EINVAL;
        }
        return 0;
    case KDGETMODE: {
        int *mode = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)mode, sizeof(int), 1)) return -EFAULT;
        *mode = gfx_owner ? KD_GRAPHICS : KD_TEXT;
        return 0;
    }

    // Keyboard mode. Programs such as the Nano-X tty driver ask for it (and put it back when
    // they leave). Only translated characters exist here, so any other mode is refused
    // instead of pretending: a program expecting scancodes would silently read garbage.
    case KDGKBMODE: {
        int *mode = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)mode, sizeof(int), 1)) return -EFAULT;
        *mode = kbd_mode;
        return 0;
    }
    case KDSKBMODE:
        if (arg != K_XLATE) return -EINVAL;
        kbd_mode = K_XLATE;
        return 0;
    case KDGKBTYPE: {                                // "is this a console?": a 101-key keyboard
        char *type = (char *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)type, 1, 1)) return -EFAULT;
        *type = KB_101;
        return 0;
    }

    // Virtual terminals. There is exactly one console (VT 1), so these only have to look
    // like a console with a single terminal. Switching never happens, so the signals a
    // VT_PROCESS program asks for are accepted but never sent.
    case VT_OPENQRY: {
        int *n = (int *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)n, sizeof(int), 1)) return -EFAULT;
        *n = 1;
        return 0;
    }
    case VT_GETSTATE: {
        struct vt_stat *st = (struct vt_stat *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)st, sizeof(*st), 1)) return -EFAULT;
        st->v_active = 1;
        st->v_signal = 0;
        st->v_state  = 3;                            // bit 0 (the unused console 0) and VT 1
        return 0;
    }
    case VT_GETMODE: {
        struct vt_mode *m = (struct vt_mode *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)m, sizeof(*m), 1)) return -EFAULT;
        *m = vt_mode_saved;
        return 0;
    }
    case VT_SETMODE: {
        const struct vt_mode *m = (const struct vt_mode *)(uintptr_t)arg;
        if (!paging_check_user((uint32_t)(uintptr_t)m, sizeof(*m), 0)) return -EFAULT;
        if (m->mode != VT_AUTO && m->mode != VT_PROCESS) return -EINVAL;
        vt_mode_saved = *m;
        return 0;
    }
    case VT_ACTIVATE:
    case VT_WAITACTIVE:
        return arg == 1 ? 0 : -EINVAL;               // there is only VT 1, and it is already active
    case VT_RELDISP:
        return 0;
    case VT_LOCKSWITCH:
    case VT_UNLOCKSWITCH:
        return 0;
    default:
        return -ENOTTY;
    }
}

// ioctl(fd, request, arg): 54
static int sys_ioctl(struct registers *r) {
    int fd = (int)r->ebx;
    uint32_t req = r->ecx, arg = r->edx;
    struct vfs_file *f = (fd >= 0 && fd < VFS_MAX_FDS) ? current->fds[fd] : NULL;

    if (!f) return -EBADF;

    if (f->node == tty_console()) return console_ioctl(req, arg);
    if (f->node->ops && f->node->ops->ioctl) return f->node->ops->ioctl(f->node, req, arg);
    return -ENOTTY;                                  // not a terminal: isatty() says no
}

void sys_tty_init(void) {
    syscall_register(54, sys_ioctl);
}