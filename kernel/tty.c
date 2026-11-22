#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include "tty.h"
#include "keyboard.h"
#include "terminal.h"
#include "proc.h"
#include "pit.h"
#include "vfs.h"

#define LINE_MAX 256

static char     line[LINE_MAX];     // line being edited          
static uint32_t line_len;
static char     ready[LINE_MAX];    // completed line, being read 
static uint32_t ready_len, ready_pos;

static int canon = 1;               // line editing on                     
static int echo  = 1;
static int isig  = 1;               // Ctrl-C and Ctrl-\ raise signals     
static int vmin  = 1;               // non-canonical reads: minimum bytes (0 or 1 supported) 
static int vtime = 0;               // ... and the timeout in tenths of a second when vmin is 0 
static int fg_pgrp;

static int esc_state;               // line mode swallows escape sequences (arrow keys etc.) 
static int eof_flag;                // Ctrl-D was typed on an empty line 
static int pushback = -1;           // a key that poll() looked at but nobody has read yet 

static void reset_input(void) {
    line_len = 0;
    ready_len = ready_pos = 0;
    esc_state = 0;
    eof_flag = 0;
    pushback = -1;
}

void tty_set_echo(int on) {
    echo = (on != 0);
}

void tty_set_raw(int on) {
    int was = canon;

    canon = !on;
    isig  = !on;
    echo  = !on;                    // raw programs draw their own screen; coming back restores echo 
    vmin  = 1;
    vtime = 0;
    if (was != canon) reset_input();
}

void tty_configure(int c, int e, int s, int vm, int vt) {
    int changed = (canon != (c != 0));

    canon = (c != 0);
    echo  = (e != 0);
    isig  = (s != 0);
    vmin  = vm ? 1 : 0;
    vtime = vt < 0 ? 0 : vt;
    if (changed) reset_input();
}

void tty_get_flags(int *c, int *e, int *s, int *vm, int *vt) {
    *c = canon;
    *e = echo;
    *s = isig;
    *vm = vmin;
    *vt = vtime;
}

int tty_wants_signals(void) {
    return isig;
}

void tty_set_fg(int pgrp) {
    fg_pgrp = pgrp;
}

int tty_get_fg(void) {
    return fg_pgrp;
}

// Ctrl-C and friends. Runs in interrupt context, but never inside other terminal code,
// because kernel code runs with interrupts off. 
void tty_interrupt(int sig) {
    line_len = 0;
    esc_state = 0;
    memset(line, 0, sizeof(line));

    if (echo) terminal_write(sig == 2 ? "^C\n" : "^\\\n", 3);

    if (fg_pgrp <= 0 || proc_kill_group(fg_pgrp, sig) == 0) {
        // nobody to interrupt: the shell is waiting at its prompt, so hand it an empty line 
        if (ready_pos == ready_len) {
            ready[0] = '\n';
            ready_len = 1;
            ready_pos = 0;
        }
    }
    sched_wake(P_SLEEP_TTY);
}

static int next_key(void) {
    if (pushback >= 0) {
        int c = pushback;
        pushback = -1;
        return c;
    }
    return keyboard_poll();
}

static void raw_echo(int c) {
    if (echo && (c == '\n' || (c >= 32 && c < 127) || c >= 160)) terminal_putc((char)c);
}

static int raw_read(char *buf, uint32_t len, int nonblock) {
    int c;

    if (vmin == 0) {                        // don't wait for ever: return what is there 
        uint32_t deadline = pit_ticks() + (uint32_t)vtime * pit_freq() / 10;

        while ((c = next_key()) < 0) {
            if (nonblock) return -EAGAIN;
            if (vtime == 0 || (int32_t)(pit_ticks() - deadline) >= 0) return 0;
            if (proc_sleep(1) < 0) return -EINTR;
        }
    } else {
        while ((c = next_key()) < 0) {
            if (nonblock) return -EAGAIN;
            sched_sleep(P_SLEEP_TTY);
            if (current->killed) return -EINTR;
        }
    }

    uint32_t n = 0;
    raw_echo(c);
    buf[n++] = (char)c;
    while (n < len && (c = next_key()) >= 0) {
        raw_echo(c);
        buf[n++] = (char)c;
    }
    return (int)n;
}

// Handles the keys that are waiting. Returns 1 once a complete line (or an end of file) is ready. 
static int cooked_pump(void) {
    for (;;) {
        if (ready_pos < ready_len || eof_flag) return 1;

        int c = next_key();
        if (c < 0) return 0;

        if (esc_state == 1) { esc_state = (c == '[') ? 2 : 0; continue; }
        if (esc_state == 2) { if (c >= 0x40 && c <= 0x7E) esc_state = 0; continue; }
        if (c == 27) { esc_state = 1; continue; }

        if (c == 4) {                       // Ctrl-D: end of file on an empty line 
            if (line_len == 0) eof_flag = 1;
            continue;
        }

        if (c == '\b') {
            if (line_len > 0) {
                line_len--;
                if (echo) terminal_putc('\b');
            }
        } else if (c == '\n') {
            line[line_len++] = '\n';
            terminal_putc('\n');            // always shown, even with echo off 
            memcpy(ready, line, line_len);
            ready_len = line_len;
            ready_pos = 0;
            line_len  = 0;
            memset(line, 0, sizeof(line));  // don't leave passwords lying around 
        } else if (((c >= 32 && c < 127) || c >= 160) && line_len < LINE_MAX - 1) {
            line[line_len++] = (char)c;
            if (echo) terminal_putc((char)c);
        }
    }
}

static int cooked_read(char *buf, uint32_t len, int nonblock) {
    while (!cooked_pump()) {
        if (nonblock) return -EAGAIN;
        sched_sleep(P_SLEEP_TTY);
        if (current->killed) return -EINTR;
    }

    if (ready_pos == ready_len) {           // only the end-of-file marker is pending 
        eof_flag = 0;
        return 0;
    }

    uint32_t n = ready_len - ready_pos;
    if (n > len) n = len;
    memcpy(buf, ready + ready_pos, n);
    ready_pos += n;

    if (ready_pos == ready_len)
        memset(ready, 0, sizeof(ready));
    return (int)n;
}

int tty_read(char *buf, uint32_t len) {
    int nonblock = vfs_nonblock;            // look now: another process may change it while we sleep 

    if (len == 0) return 0;
    return canon ? cooked_read(buf, len, nonblock) : raw_read(buf, len, nonblock);
}

// ---------- the console as a VFS character device ---------- //

static int console_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    (void)n; (void)off;
    return tty_read(buf, len);
}

static int console_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    (void)n; (void)off;
    terminal_write(buf, len);
    return (int)len;
}

static int console_poll(struct vfs_node *n) {
    (void)n;
    int ev = POLLOUT;

    if (!canon) {
        if (pushback < 0) pushback = keyboard_poll();
        if (pushback >= 0) ev |= POLLIN;
    } else if (cooked_pump()) {
        ev |= POLLIN;
    }
    return ev;
}

static struct vfs_ops console_ops = {
    .read  = console_read,
    .write = console_write,
    .poll  = console_poll,
};

static struct vfs_node console_node = {
    .ino  = 1,
    .type = VFS_CHAR,
    .refs = 1,                          // static: never freed 
    .mode = S_IFCHR | 0666,
    .ops  = &console_ops,
};

struct vfs_node *tty_console(void) {
    return &console_node;
}