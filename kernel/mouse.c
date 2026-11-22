#include <stdint.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "proc.h"
#include "vfs.h"
#include "kprintf.h"

#define RING 192                            // 64 packets of 3 bytes

static volatile uint8_t  ring[RING];
static volatile uint32_t head, tail, count; // count is in bytes
static uint8_t pkt[3];
static int pkt_len;

static void wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) return;
}

static int wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return 1;
    return 0;
}

static void mouse_cmd(uint8_t b) {          // send a byte to the mouse itself
    wait_write();
    outb(0x64, 0xD4);
    wait_write();
    outb(0x60, b);
    if (wait_read()) inb(0x60);             // its acknowledge (0xFA)
}

static void mouse_handler(struct registers *r) {
    (void)r;
    uint8_t b = inb(0x60);

    if (pkt_len == 0 && !(b & 0x08)) return;    // out of sync: the first byte always has bit 3 set
    pkt[pkt_len++] = b;
    if (pkt_len < 3) return;
    pkt_len = 0;

    if (count + 3 > RING) return;               // buffer full: drop the packet
    for (int i = 0; i < 3; i++) {
        ring[head] = pkt[i];
        head = (head + 1) % RING;
    }
    count += 3;
    sched_wake(P_SLEEP_TTY);                    // wakes sleepers; they re-check their own condition
}

// Answers to PS/2 commands (see mouse_write) wait here until the reader fetches them.
// As on Linux's /dev/input/mice, a new command replaces the answer to the previous one.
static uint8_t  resp[4];
static uint32_t resp_len, resp_pos;
static uint32_t pkt_off;                    // bytes of the oldest packet already handed out (0..2)

// Like Linux's mousedev, a read returns the answer to a command first, then ONE movement
// packet at a time: a driver that asks for 4 bytes (to allow for the wheel byte of the
// IntelliMouse protocol) must get 3 bytes, a single packet, and never part of the next.
// A reader that asks for fewer bytes than a packet gets the rest on its next read.
static int mouse_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    int nonblock = vfs_nonblock;
    uint8_t *dst = buf;
    uint32_t out = 0;
    (void)n; (void)off;

    if (len == 0) return 0;

    while (resp_pos == resp_len && count == 0) {
        if (nonblock) return -EAGAIN;
        sched_sleep(P_SLEEP_TTY);
        if (current->killed) return -EINTR;
    }

    if (resp_pos < resp_len) {              // the answer to a command comes first
        while (resp_pos < resp_len && out < len) dst[out++] = resp[resp_pos++];
        return (int)out;
    }

    while (out < len && pkt_off < 3) {      // the rest of the oldest packet, or a whole new one
        dst[out++] = ring[tail];
        tail = (tail + 1) % RING;
        count--;
        pkt_off++;
    }
    if (pkt_off == 3) pkt_off = 0;
    return (int)out;
}

// ---------- PS/2 commands written to the device ---------- //

// On Linux, /dev/input/mice and /dev/psaux accept PS/2 commands and the mouse's answer
// comes back through read(). Drivers rely on that: they send "enable reporting" or ask
// for the device id and wait for the 0xFA acknowledge. The real mouse was already set up
// by mouse_init() and stays in stream mode, so the commands are answered here by a small
// emulation of a plain 3-button mouse (device id 0, 100 samples/s, 4 counts/mm). It
// never switches to the 4-byte wheel protocol, even after the IntelliMouse "magic"
// sequence: drivers that ask for the wheel just see a mouse that has none.

static int expect_arg;                      // the next byte is the argument of a set-rate / set-resolution command

static void answer(uint32_t n, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    resp[0] = a; resp[1] = b; resp[2] = c; resp[3] = d;
    resp_len = n;
    resp_pos = 0;
}

static int mouse_write(struct vfs_node *n, uint32_t off, uint32_t len, const void *buf) {
    const uint8_t *p = buf;
    (void)n; (void)off;

    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = p[i];

        if (expect_arg) {                   // the argument byte
            expect_arg = 0;
            answer(1, 0xFA, 0, 0, 0);
            continue;
        }

        switch (b) {
        case 0xFF:                          // reset: acknowledge, self-test passed, device id 0
            pkt_len = 0;
            answer(3, 0xFA, 0xAA, 0x00, 0);
            break;
        case 0xF2:                          // get device id: a plain mouse, no wheel
            answer(2, 0xFA, 0x00, 0, 0);
            break;
        case 0xE9:                          // status request: flags, resolution, sample rate
            answer(4, 0xFA, 0x00, 0x02, 0x64);
            break;
        case 0xEB:                          // read data: one packet with no movement
            answer(4, 0xFA, 0x08, 0x00, 0x00);
            break;
        case 0xF3:                          // set sample rate
        case 0xE8:                          // set resolution
            answer(1, 0xFA, 0, 0, 0);
            expect_arg = 1;
            break;
        default:                            // enable, disable, defaults, scaling, stream mode ...
            answer(1, 0xFA, 0, 0, 0);
            break;
        }
    }
    sched_wake(P_SLEEP_TTY);                // a reader may be waiting for the answer
    return (int)len;
}

static int mouse_poll(struct vfs_node *n) {
    (void)n;
    return (resp_pos < resp_len || count > 0) ? POLLIN : 0;
}

static struct vfs_ops mouse_ops = {
    .read  = mouse_read,
    .write = mouse_write,
    .poll  = mouse_poll,
};

static struct vfs_node mouse_vnode = {
    .ino  = 21,
    .type = VFS_CHAR,
    .refs = 1,
    .mode = S_IFCHR | 0666,
    .ops  = &mouse_ops,
};

struct vfs_node *mouse_node(void) {
    return &mouse_vnode;
}

void mouse_init(void) {
    uint8_t cfg;

    wait_write();
    outb(0x64, 0xA8);                           // enable the second PS/2 port

    wait_write();
    outb(0x64, 0x20);                           // read the controller configuration byte
    if (!wait_read()) { kprintf("mouse: no PS/2 controller\n"); return; }
    cfg = inb(0x60);
    cfg |= 0x02;                                // enable IRQ 12
    cfg &= (uint8_t)~0x20;                      // enable the second port's clock
    wait_write();
    outb(0x64, 0x60);
    wait_write();
    outb(0x60, cfg);

    mouse_cmd(0xF6);                            // defaults
    mouse_cmd(0xF4);                            // start sending packets

    isr_register_handler(44, mouse_handler);    // IRQ 12 = vector 32 + 12
    pic_unmask(12);
    kprintf("mouse: PS/2 ready\n");
}