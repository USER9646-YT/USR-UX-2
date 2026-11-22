#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <stdint.h>

struct vfs_node;

// Line mode: blocks until a full line was typed. Raw mode: returns as soon as any key is
// available. Returns 0 on end-of-file (Ctrl-D at an empty line).
int tty_read(char *buf, uint32_t len);

void tty_set_echo(int on);      // typed characters are echoed (1) or not (0)
void tty_set_raw(int on);       // 1 = raw (no line editing, no echo, no signals), 0 = normal

// termios-style control: canonical (no not the one big corpo that makes ub*ntu) (line) mode, echo, signal keys, VMIN / VTIME
void tty_configure(int canon, int echo, int isig, int vmin, int vtime);
void tty_get_flags(int *canon, int *echo, int *isig, int *vmin, int *vtime);

// Ctrl-C / Ctrl-\ handling
int  tty_wants_signals(void);           // 1 when the terminal turns those keys into signals
void tty_interrupt(int sig);            // called by the keyboard driver (interrupt context)
void tty_set_fg(int pgrp);              // the foreground process group; 0 = none
int  tty_get_fg(void);

// The console as a VFS character device (stdin/stdout/stderr of every process)
struct vfs_node *tty_console(void);

#endif