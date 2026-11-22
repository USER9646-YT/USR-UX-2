#include <stdint.h>
#include <signal.h>
#include "keyboard.h"
#include "keymap.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "proc.h"
#include "tty.h"

#define BUF_SIZE 256
static volatile char     buf[BUF_SIZE];
static volatile uint32_t head, tail;

static int lshift, rshift, lctrl, rctrl, altgr, caps;
static int ext;                         // previous byte was 0xE0 
static int pause_skip;                  // bytes left of the Pause key sequence 

static int layout_index;                // 0 = the first layout (US English) 

void keyboard_set_layout(int index) {
    if (keymap_get(index)) layout_index = index;
}

int keyboard_layout(void) {
    return layout_index;
}

static void push(char c) {
    uint32_t next = (head + 1) % BUF_SIZE;
    if (next != tail) {                 // drop the key if the buffer is full 
        buf[head] = c;
        head = next;
    }
}

// All or nothing, so an escape sequence is never cut in half 
static void push_str(const char *s) {
    uint32_t room = (tail + BUF_SIZE - head - 1) % BUF_SIZE;
    uint32_t n = 0;
    while (s[n]) n++;
    if (n > room) return;
    while (*s) push(*s++);
}

static void handle_extended(uint8_t code, int release) {
    switch (code) {
    case 0x1D: rctrl = !release; return;                    // right ctrl 
    case 0x38: altgr = !release; return;                    // right alt = AltGr 
    case 0x1C: if (!release) push('\n'); return;            // keypad enter 
    case 0x35: if (!release) push('/');  return;            // keypad slash 
    default: break;
    }
    if (release) return;

    switch (code) {
    case 0x48: push_str("\033[A");  break;                  // up 
    case 0x50: push_str("\033[B");  break;                  // down 
    case 0x4D: push_str("\033[C");  break;                  // right 
    case 0x4B: push_str("\033[D");  break;                  // left 
    case 0x47: push_str("\033[H");  break;                  // home 
    case 0x4F: push_str("\033[F");  break;                  // end 
    case 0x49: push_str("\033[5~"); break;                  // page up 
    case 0x51: push_str("\033[6~"); break;                  // page down 
    case 0x52: push_str("\033[2~"); break;                  // insert 
    case 0x53: push_str("\033[3~"); break;                  // delete 
    default: break;
    }
}

// Caps Lock flips the case of letters, including the German umlauts 
static uint8_t flip_case(uint8_t c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 32);
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
    if (c == 0xE4 || c == 0xF6 || c == 0xFC) return (uint8_t)(c - 32);     // a o u umlaut -> capital 
    if (c == 0xC4 || c == 0xD6 || c == 0xDC) return (uint8_t)(c + 32);
    return c;
}

static void kb_handler(struct registers *r) {
    (void)r;
    uint8_t sc = inb(0x60);

    if (pause_skip) { pause_skip--; return; }
    if (sc == 0xE1) { pause_skip = 5; return; }             // Pause key: ignore it 
    if (sc == 0xE0) { ext = 1; return; }

    int release = sc & 0x80;
    uint8_t code = sc & 0x7F;

    if (ext) {
        ext = 0;
        handle_extended(code, release);
        sched_wake(P_SLEEP_TTY);
        return;
    }

    if (code == 0x2A) { lshift = !release; return; }
    if (code == 0x36) { rshift = !release; return; }
    if (code == 0x1D) { lctrl  = !release; return; }
    if (code == 0x3A && !release) { caps = !caps; return; }
    if (release) return;

    int shift = lshift || rshift;
    int ctrl  = lctrl || rctrl;
    uint8_t c = keymap_char(keymap_get(layout_index), code, shift, altgr);

    if (caps) c = flip_case(c);
    if (ctrl) {
        if (c >= 'a' && c <= 'z')      c = (uint8_t)(c & 0x1F);
        else if (c >= 'A' && c <= 'Z') c = (uint8_t)(c & 0x1F);
        else if (c == '[')             c = 27;
        else if (c == '\\')            c = 0x1C;
    }
    if (!c) return;

    // Ctrl-C and Ctrl-\ interrupt the foreground job, even if it never reads the keyboard 
    if ((c == 3 || c == 0x1C) && tty_wants_signals()) {
        tty_interrupt(c == 3 ? SIGINT : SIGQUIT);
        return;
    }

    push((char)c);
    sched_wake(P_SLEEP_TTY);                    // wake whoever waits for input 
}

void keyboard_init(void) {
    isr_register_handler(33, kb_handler);
    pic_unmask(1);
}

int keyboard_poll_scancode(void) {
    for (;;) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) return -1;           // output buffer empty 
        uint8_t sc = inb(0x60);
        if (status & 0x20) continue;            // a mouse byte: throw it away 
        return sc;
    }
}

int keyboard_poll(void) {
    if (head == tail) return -1;
    char c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return (unsigned char)c;
}

char keyboard_getchar(void) {
    while (head == tail)
        __asm__ volatile ("hlt");
    char c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}