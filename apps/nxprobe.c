// nxprobe: checks the OS facilities that Nano-X's Linux drivers rely on, one by one,
// before any Nano-X code is involved.

// Phase 1 (automatic, don't touch keyboard or mouse): framebuffer, terminal, mouse,
// timing and memory checks. Phase 2 (interactive): graphics mode with a square that
// follows the mouse, driven by a select() loop like Nano-X's main loop.

// Run it from the console. Environment: FRAMEBUFFER (default /dev/fb0) and
// MOUSE_PORT (default /dev/psaux), the same variables Nano-X uses.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>

// keyboard-mode ioctls that the Linux tty keyboard driver uses
#ifndef KDGKBMODE
#define KDGKBMODE 0x4B44
#endif
#ifndef KDSKBMODE
#define KDSKBMODE 0x4B45
#endif
#ifndef K_XLATE
#define K_XLATE 0x01
#endif

#define TEST_SECONDS 20
#define SQUARE       24

static int n_ok, n_warn, n_fail;

static void report(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void report(const char *tag, const char *fmt, ...) {
    va_list ap;

    printf("[%s] ", tag);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

#define OK(...)   do { n_ok++;   report(" ok ", __VA_ARGS__); } while (0)
#define WARN(...) do { n_warn++; report("warn", __VA_ARGS__); } while (0)
#define FAIL(...) do { n_fail++; report("FAIL", __VA_ARGS__); } while (0)
#define INFO(...) report("info", __VA_ARGS__)

static long base_sec;

static long now_ms(void) {                          // milliseconds since the program started
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (!base_sec) base_sec = tv.tv_sec;
    return (long)(tv.tv_sec - base_sec) * 1000L + (long)(tv.tv_usec / 1000);
}

// Reads up to n bytes from fd, waiting at most timeout_ms in total. Returns how many arrived.
static int read_with_timeout(int fd, unsigned char *buf, int n, long timeout_ms) {
    int got = 0;
    long deadline = now_ms() + timeout_ms;

    while (got < n) {
        long left = deadline - now_ms();
        if (left <= 0) break;

        fd_set rf;
        struct timeval tv = { left / 1000, (left % 1000) * 1000 };
        FD_ZERO(&rf);
        FD_SET(fd, &rf);
        if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0) break;

        ssize_t r = read(fd, buf + got, (size_t)(n - got));
        if (r <= 0) break;
        got += (int)r;
    }
    return got;
}

// ---------- state shared by both phases ----------

static struct termios orig_term;
static int have_orig;
static int fb_fd = -1, mouse_fd = -1;
static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;
static unsigned char *fbmem;
static unsigned int stride;
static int bytespp;

static void raw_terminal(int vmin, int vtime) {
    struct termios t = orig_term;

    t.c_iflag &= ~(unsigned)(ICRNL | IXON);
    t.c_lflag &= ~(unsigned)(ICANON | ECHO | ISIG | IEXTEN);
    t.c_cc[VMIN] = (cc_t)vmin;
    t.c_cc[VTIME] = (cc_t)vtime;
    tcsetattr(0, TCSANOW, &t);
}

static void restore_console(void) {
    ioctl(0, KDSETMODE, KD_TEXT);
    if (have_orig) tcsetattr(0, TCSANOW, &orig_term);
}

// ---------- phase 1 ----------

static void check_framebuffer(void) {
    const char *path = getenv("FRAMEBUFFER");
    if (!path || !*path) path = "/dev/fb0";

    printf("\n--- framebuffer (%s) ---\n", path);
    fb_fd = open(path, O_RDWR);
    if (fb_fd < 0) { FAIL("open %s: %s", path, strerror(errno)); return; }
    OK("open %s", path);

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vi) < 0) { FAIL("FBIOGET_VSCREENINFO: %s", strerror(errno)); return; }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fi) < 0) { FAIL("FBIOGET_FSCREENINFO: %s", strerror(errno)); return; }

    INFO("mode %ux%u, %u bits per pixel (virtual %ux%u)", vi.xres, vi.yres, vi.bits_per_pixel,
         vi.xres_virtual, vi.yres_virtual);
    INFO("red %u/%u  green %u/%u  blue %u/%u  (offset/length)", vi.red.offset, vi.red.length,
         vi.green.offset, vi.green.length, vi.blue.offset, vi.blue.length);
    INFO("line_length %u, smem_len %u, smem_start %lx, id \"%.16s\"", fi.line_length, fi.smem_len,
         fi.smem_start, fi.id);

    if (vi.xres == 0 || vi.yres == 0) { FAIL("the screen size is zero"); return; }

    if (vi.bits_per_pixel == 32 || vi.bits_per_pixel == 16) OK("%u bits per pixel is supported by Nano-X", vi.bits_per_pixel);
    else if (vi.bits_per_pixel == 24) WARN("24 bits per pixel works in Nano-X but is slow");
    else WARN("%u bits per pixel: Nano-X would need a palette (FBIOPUTCMAP), which this kernel doesn't have", vi.bits_per_pixel);

    if (vi.bits_per_pixel > 8 && (vi.red.length == 0 || vi.green.length == 0 || vi.blue.length == 0))
        FAIL("a true colour mode needs non-zero channel lengths");
    else
        OK("channel layout present");

    if (fi.line_length >= vi.xres * vi.bits_per_pixel / 8) OK("line_length covers a full row");
    else FAIL("line_length %u is smaller than one row (%u bytes)", fi.line_length, vi.xres * vi.bits_per_pixel / 8);

    if (fi.smem_len >= fi.line_length * vi.yres) OK("smem_len covers the whole screen");
    else FAIL("smem_len %u is smaller than the screen (%u bytes)", fi.smem_len, fi.line_length * vi.yres);

    if (fi.type == FB_TYPE_PACKED_PIXELS) OK("type: packed pixels");
    else WARN("type is %u, Nano-X expects packed pixels (0)", fi.type);

    if (vi.bits_per_pixel > 8 && fi.visual != FB_VISUAL_TRUECOLOR)
        WARN("visual is %u, Nano-X expects true colour (2)", fi.visual);

    struct fb_var_screeninfo again = vi;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &again) == 0) OK("FBIOPUT_VSCREENINFO accepts the current mode");
    else INFO("FBIOPUT_VSCREENINFO: %s (only matters if the Nano-X config asks to change the mode)", strerror(errno));

    void *m = mmap(NULL, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (m == MAP_FAILED) { FAIL("mmap of the framebuffer: %s", strerror(errno)); return; }
    OK("mmap of %u bytes at %p", fi.smem_len, m);

    fbmem = m;
    stride = fi.line_length;
    bytespp = (int)(vi.bits_per_pixel / 8);

    volatile unsigned char first = fbmem[0];        // touch both ends: a bad mapping faults here
    volatile unsigned char last = fbmem[fi.smem_len - 1];
    (void)first; (void)last;
    OK("first and last byte of the framebuffer are readable");
}

static void check_terminal(void) {
    printf("\n--- terminal and keyboard (don't press keys) ---\n");

    if (tcgetattr(0, &orig_term) < 0) {
        FAIL("stdin is not a terminal (%s): run nxprobe on the console, not through a pipe", strerror(errno));
        return;
    }
    have_orig = 1;
    OK("tcgetattr on stdin");

    raw_terminal(1, 0);
    struct termios chk;
    if (tcgetattr(0, &chk) == 0 && !(chk.c_lflag & (ICANON | ECHO)))
        OK("raw mode (ICANON and ECHO off) was applied and reads back");
    else
        WARN("tcsetattr was accepted but the flags did not change");

    fd_set rf;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&rf);
    FD_SET(0, &rf);
    int r = select(1, &rf, NULL, NULL, &tv);
    if (r == 0) OK("select() on the keyboard with a zero timeout returns 0 when nothing is typed");
    else if (r > 0) INFO("select() says a key is waiting (you pressed something)");
    else WARN("select() on the keyboard failed: %s", strerror(errno));

    int fl = fcntl(0, F_GETFL);
    char c;
    if (fl >= 0 && fcntl(0, F_SETFL, fl | O_NONBLOCK) == 0) {
        ssize_t n = read(0, &c, 1);
        if (n < 0 && errno == EAGAIN) OK("a non-blocking read on the keyboard returns EAGAIN");
        else if (n == 1) INFO("a key was waiting in the non-blocking read");
        else WARN("a non-blocking keyboard read returned %zd (%s)", n, n < 0 ? strerror(errno) : "end of file");
        fcntl(0, F_SETFL, fl);
    } else {
        WARN("fcntl(O_NONBLOCK) on the keyboard failed: %s", strerror(errno));
    }

    raw_terminal(0, 1);                              // VMIN=0, VTIME=1: wait at most 100 ms
    long t0 = now_ms();
    ssize_t n = read(0, &c, 1);
    long dt = now_ms() - t0;
    if (n == 0 && dt >= 50 && dt <= 400) OK("VMIN=0/VTIME=1 read timed out after %ld ms", dt);
    else if (n == 1) INFO("a key arrived during the VTIME test");
    else WARN("VMIN=0/VTIME=1 read returned %zd after %ld ms (expected 0 after about 100)", n, dt);

    int km = -1;
    if (ioctl(0, KDGKBMODE, &km) == 0) OK("KDGKBMODE reports mode %d", km);
    else WARN("KDGKBMODE: %s. Nano-X's tty keyboard driver asks for it; add a harmless handler to the kernel", strerror(errno));

    if (ioctl(0, KDSKBMODE, K_XLATE) == 0) OK("KDSKBMODE accepts K_XLATE");
    else WARN("KDSKBMODE: %s. Same as above", strerror(errno));

    int cm = -1;
    if (ioctl(0, KDGETMODE, &cm) == 0) OK("KDGETMODE reports %s", cm == KD_GRAPHICS ? "graphics" : "text");
    else WARN("KDGETMODE: %s", strerror(errno));

    struct vt_stat vs;
    if (ioctl(0, VT_GETSTATE, &vs) == 0) OK("VT_GETSTATE: active console %u", (unsigned)vs.v_active);
    else WARN("VT_GETSTATE: %s (programs that look at the virtual terminal state will fail)", strerror(errno));

    int free_vt = -1;
    if (ioctl(0, VT_OPENQRY, &free_vt) == 0) OK("VT_OPENQRY: first free console is %d", free_vt);
    else WARN("VT_OPENQRY: %s", strerror(errno));

    struct vt_mode vm;
    if (ioctl(0, VT_GETMODE, &vm) == 0) OK("VT_GETMODE: mode %d (0 = automatic switching)", (int)vm.mode);
    else WARN("VT_GETMODE: %s", strerror(errno));

    tcsetattr(0, TCSANOW, &orig_term);
}

static void check_mouse(void) {
    const char *path = getenv("MOUSE_PORT");
    if (!path || !*path) path = "/dev/psaux";

    printf("\n--- mouse (%s, don't move it) ---\n", path);
    int rw = 1;
    mouse_fd = open(path, O_RDWR | O_NONBLOCK);      // a driver that sends init commands opens it read/write
    if (mouse_fd < 0) { rw = 0; mouse_fd = open(path, O_RDONLY | O_NONBLOCK); }
    if (mouse_fd < 0) { FAIL("open %s: %s", path, strerror(errno)); return; }
    if (rw) OK("open %s read/write (non-blocking)", path);
    else    WARN("open %s works read-only but not read/write: a driver that sends init commands will fail", path);

    unsigned char b[3];
    ssize_t n = read(mouse_fd, b, 1);
    if (n < 0 && errno == EINVAL)
        WARN("a 1-byte read is rejected with EINVAL: a driver that reads byte by byte can't work. "
             "Make the kernel's mouse_read accept any length");
    else if (n < 0 && errno == EAGAIN) OK("a 1-byte read returns EAGAIN when there is no data");
    else if (n >= 0) OK("a 1-byte read works (got %zd)", n);
    else WARN("a 1-byte read failed: %s", strerror(errno));

    n = read(mouse_fd, b, 3);
    if (n < 0 && errno == EAGAIN) OK("a 3-byte read returns EAGAIN when there is no data");
    else if (n == 3) INFO("a packet was waiting: %02x %02x %02x", b[0], b[1], b[2]);
    else WARN("a 3-byte read returned %zd (%s)", n, n < 0 ? strerror(errno) : "end of file");

    if (rw) {
        unsigned char ans[4];

        unsigned char enable = 0xF4;                 // the PS/2 "enable reporting" command
        n = write(mouse_fd, &enable, 1);
        if (n != 1) {
            INFO("writing a command byte: %s (drivers that send init commands may complain)", strerror(errno));
        } else {
            OK("writing a command byte to the mouse is accepted");
            int got = read_with_timeout(mouse_fd, ans, 1, 200);
            if (got == 1 && ans[0] == 0xFA) OK("the command is answered with ACK (0xFA), as a real PS/2 mouse does");
            else if (got == 1) INFO("a byte arrived after the command: %02x (a movement packet, not an ACK)", ans[0]);
            else WARN("no ACK came back after the command: drivers that wait for the acknowledge will time out");
        }

        unsigned char getid = 0xF2;                  // "get device id", used to detect wheel mice
        if (write(mouse_fd, &getid, 1) == 1) {
            int got = read_with_timeout(mouse_fd, ans, 2, 200);
            if (got == 2 && ans[0] == 0xFA && ans[1] == 0x00) OK("device id query: ACK and id 0 (a plain 3-byte mouse)");
            else if (got >= 1 && ans[0] == 0xFA) WARN("device id query: ACK but %d more byte(s) instead of the id", got - 1);
            else INFO("device id query returned %d byte(s)", got);
        }
    }

    fd_set rf;
    struct timeval tv = { 0, 50000 };
    FD_ZERO(&rf);
    FD_SET(mouse_fd, &rf);
    int r = select(mouse_fd + 1, &rf, NULL, NULL, &tv);
    if (r == 0) OK("select() on the mouse times out when it is still");
    else if (r > 0) INFO("select() reports mouse data (the mouse moved)");
    else WARN("select() on the mouse failed: %s", strerror(errno));
}

static void check_system(void) {
    printf("\n--- timing, memory, libc ---\n");

    long t0 = now_ms();
    usleep(50000);
    long dt = now_ms() - t0;
    if (dt >= 40 && dt <= 150) OK("usleep(50 ms) took %ld ms", dt);
    else WARN("usleep(50 ms) took %ld ms", dt);

    struct timeval tv = { 0, 100000 };
    t0 = now_ms();
    select(0, NULL, NULL, NULL, &tv);
    dt = now_ms() - t0;
    if (dt >= 80 && dt <= 250) OK("select() as a 100 ms timer took %ld ms", dt);
    else WARN("select() as a 100 ms timer took %ld ms (Nano-X relies on this)", dt);

    size_t big = 4u * 1024u * 1024u;
    unsigned char *p = malloc(big);
    if (p) {
        memset(p, 0x5A, big);
        int good = (p[0] == 0x5A && p[big - 1] == 0x5A);
        free(p);
        if (good) OK("malloc + memset of 4 MB (a screen-sized back buffer)");
        else FAIL("4 MB buffer did not keep its contents");
    } else {
        FAIL("malloc(4 MB) failed");
    }

    p = malloc(16u * 1024u * 1024u);
    if (p) { OK("malloc of 16 MB"); free(p); }
    else WARN("malloc(16 MB) failed: large images or fonts may not load");

    if (sysconf(_SC_PAGESIZE) == 4096) OK("page size is 4096");
    else WARN("page size reported as %ld", sysconf(_SC_PAGESIZE));

    if (signal(SIGUSR1, SIG_IGN) != SIG_ERR)
        INFO("signal() accepts handlers but they never run: keep VTSWITCH off and avoid SIGALRM timers");
    else
        WARN("signal() failed: %s", strerror(errno));

    INFO("TERM=%s HOME=%s", getenv("TERM") ? getenv("TERM") : "(unset)", getenv("HOME") ? getenv("HOME") : "(unset)");
}

// ---------- phase 2: graphics and input loop ---------- //

static unsigned int pack(unsigned int r, unsigned int g, unsigned int b) {
    return ((r >> (8 - vi.red.length)) << vi.red.offset) |
           ((g >> (8 - vi.green.length)) << vi.green.offset) |
           ((b >> (8 - vi.blue.length)) << vi.blue.offset);
}

static void put_px(int x, int y, unsigned int color) {
    unsigned char *p = fbmem + (unsigned int)y * stride + (unsigned int)x * (unsigned int)bytespp;

    if (bytespp == 4) *(volatile uint32_t *)p = color;
    else              *(volatile uint16_t *)p = (uint16_t)color;
}

static unsigned int bg_color(int y) {
    unsigned int t = (unsigned int)y * 255u / vi.yres;
    return pack(10, 30 + t / 3, 80 + t / 2);
}

static void fill_bg(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h && y < (int)vi.yres; y++) {
        unsigned int c = bg_color(y);
        for (int x = x0; x < x0 + w && x < (int)vi.xres; x++) put_px(x, y, c);
    }
}

static void fill_box(int x0, int y0, int w, int h, unsigned int c) {
    for (int y = y0; y < y0 + h && y < (int)vi.yres; y++)
        for (int x = x0; x < x0 + w && x < (int)vi.xres; x++) put_px(x, y, c);
}

static void interactive(void) {
    printf("\n--- phase 2: graphics, mouse and keyboard ---\n");

    if (!fbmem || (bytespp != 4 && bytespp != 2)) {
        WARN("skipping phase 2: it needs a mapped 16 or 32 bit framebuffer");
        return;
    }
    if (!have_orig) {
        WARN("skipping phase 2: stdin is not a terminal");
        return;
    }

    printf("The screen will switch to graphics mode for %d seconds.\n"
           "Move the mouse (the square follows, left button = red, right = blue).\n"
           "Press q to leave early. Press Enter to start. ", TEST_SECONDS);
    fflush(stdout);
    if (getchar() == EOF) return;

    raw_terminal(1, 0);
    ioctl(0, KDSETMODE, KD_GRAPHICS);
    fill_bg(0, 0, (int)vi.xres, (int)vi.yres);

    int x = (int)vi.xres / 2, y = (int)vi.yres / 2;
    unsigned char mbuf[96];
    int mlen = 0;
    long packets = 0, resyncs = 0, keys = 0, wakeups = 0, timeouts = 0, mouse_reads = 0;
    int beat = 0, quit = 0;
    long start = now_ms();

    fill_box(x, y, SQUARE, SQUARE, pack(255, 255, 255));

    while (!quit && now_ms() - start < TEST_SECONDS * 1000L) {
        fd_set rf;
        struct timeval tv = { 0, 250000 };
        int maxfd = 0;

        FD_ZERO(&rf);
        FD_SET(0, &rf);
        if (mouse_fd >= 0) { FD_SET(mouse_fd, &rf); if (mouse_fd > maxfd) maxfd = mouse_fd; }

        int r = select(maxfd + 1, &rf, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {                                // heartbeat: a blinking dot proves the loop is alive
            timeouts++;
            beat = !beat;
            fill_box(4, 4, 8, 8, beat ? pack(255, 255, 0) : pack(0, 0, 0));
            continue;
        }
        wakeups++;

        if (FD_ISSET(0, &rf)) {
            char kb[16];
            ssize_t n = read(0, kb, sizeof(kb));
            for (ssize_t i = 0; i < n; i++) {
                keys++;
                if (kb[i] == 'q') quit = 1;
            }
        }

        if (mouse_fd >= 0 && FD_ISSET(mouse_fd, &rf)) {
            ssize_t n = read(mouse_fd, mbuf + mlen, sizeof(mbuf) - (size_t)mlen);
            mouse_reads++;
            if (n > 0) mlen += (int)n;

            int i = 0, buttons = 0;
            while (mlen - i >= 3) {
                if (!(mbuf[i] & 8)) { resyncs++; i++; continue; }       // bit 3 must always be set
                int dx = mbuf[i + 1] - ((mbuf[i] << 4) & 0x100);
                int dy = mbuf[i + 2] - ((mbuf[i] << 3) & 0x100);

                fill_bg(x, y, SQUARE, SQUARE);
                x += dx;
                y -= dy;
                if (x < 0) x = 0;
                if (y < 0) y = 0;
                if (x > (int)vi.xres - SQUARE) x = (int)vi.xres - SQUARE;
                if (y > (int)vi.yres - SQUARE) y = (int)vi.yres - SQUARE;
                buttons = mbuf[i] & 3;
                packets++;
                i += 3;
            }
            memmove(mbuf, mbuf + i, (size_t)(mlen - i));
            mlen -= i;

            if (packets)
                fill_box(x, y, SQUARE, SQUARE,
                         (buttons & 1) ? pack(255, 40, 40) : (buttons & 2) ? pack(60, 60, 255) : pack(255, 255, 255));
        }
    }

    restore_console();
    printf("\nphase 2 finished after %ld ms.\n", now_ms() - start);
    printf("select() woke up %ld times for input and timed out %ld times;\n", wakeups, timeouts);
    printf("%ld mouse packets (%ld resyncs, %ld reads), %ld key bytes.\n", packets, resyncs, mouse_reads, keys);

    if (timeouts == 0) WARN("the 250 ms select timeout never fired: it should fire about 4 times a second when idle");
    else OK("select() timeouts fired (%ld)", timeouts);

    if (mouse_fd >= 0) {
        if (packets > 0 && resyncs == 0) OK("mouse packets arrived in sync");
        else if (packets > 0) WARN("%ld resyncs: the mouse stream lost alignment", resyncs);
        else INFO("no mouse packets: you didn't move it, or the mouse isn't reporting");
    }
    if (keys > 0) OK("keyboard bytes arrived through select/read");
}

int main(void) {
    now_ms();
    printf("nxprobe: checking what Nano-X's Linux drivers need\n");

    check_framebuffer();
    check_terminal();
    check_mouse();
    check_system();
    interactive();

    restore_console();
    printf("\n=== %d ok, %d warnings, %d failures ===\n", n_ok, n_warn, n_fail);
    if (n_fail == 0 && n_warn == 0) printf("The OS side looks ready for Nano-X.\n");
    else if (n_fail == 0) printf("No failures. Read the warnings: each one says what to change.\n");
    else printf("Fix the FAIL lines first; Nano-X can't start without them.\n");
    return n_fail != 0;
}