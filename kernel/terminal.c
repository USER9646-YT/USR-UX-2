#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "terminal.h"
#include "fb.h"
#include "io.h"
#include "latin1.h"

#define VGA_MEM   ((volatile uint16_t *)0xB8000)
#define TAB_WIDTH 8
#define FONT_W    8
#define FONT_H    16

/* ---------- font: ASCII 32..126, 8x8, bit 0 = leftmost pixel; drawn at double height ---------- */

static const uint8_t font8x8[95][8] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* space */
    { 0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00 }, /* ! */
    { 0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00 }, /* " */
    { 0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00 }, /* # */
    { 0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00 }, /* $ */
    { 0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00 }, /* % */
    { 0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00 }, /* & */
    { 0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00 }, /* ' */
    { 0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00 }, /* ( */
    { 0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00 }, /* ) */
    { 0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00 }, /* * */
    { 0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00 }, /* + */
    { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06 }, /* , */
    { 0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00 }, /* - */
    { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00 }, /* . */
    { 0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00 }, /* / */
    { 0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00 }, /* 0 */
    { 0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00 }, /* 1 */
    { 0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00 }, /* 2 */
    { 0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00 }, /* 3 */
    { 0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00 }, /* 4 */
    { 0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00 }, /* 5 */
    { 0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00 }, /* 6 */
    { 0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00 }, /* 7 */
    { 0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00 }, /* 8 */
    { 0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00 }, /* 9 */
    { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00 }, /* : */
    { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06 }, /* ; */
    { 0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00 }, /* < */
    { 0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00 }, /* = */
    { 0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00 }, /* > */
    { 0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00 }, /* ? */
    { 0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00 }, /* @ */
    { 0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00 }, /* A */
    { 0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00 }, /* B */
    { 0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00 }, /* C */
    { 0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00 }, /* D */
    { 0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00 }, /* E */
    { 0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00 }, /* F */
    { 0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00 }, /* G */
    { 0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00 }, /* H */
    { 0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 }, /* I */
    { 0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00 }, /* J */
    { 0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00 }, /* K */
    { 0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00 }, /* L */
    { 0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00 }, /* M */
    { 0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00 }, /* N */
    { 0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00 }, /* O */
    { 0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00 }, /* P */
    { 0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00 }, /* Q */
    { 0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00 }, /* R */
    { 0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00 }, /* S */
    { 0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 }, /* T */
    { 0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00 }, /* U */
    { 0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00 }, /* V */
    { 0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00 }, /* W */
    { 0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00 }, /* X */
    { 0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00 }, /* Y */
    { 0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00 }, /* Z */
    { 0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00 }, /* [ */
    { 0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00 }, /* \ */
    { 0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00 }, /* ] */
    { 0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00 }, /* ^ */
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF }, /* _ */
    { 0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00 }, /* ` */
    { 0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00 }, /* a */
    { 0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00 }, /* b */
    { 0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00 }, /* c */
    { 0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00 }, /* d */
    { 0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00 }, /* e */
    { 0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00 }, /* f */
    { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F }, /* g */
    { 0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00 }, /* h */
    { 0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00 }, /* i */
    { 0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E }, /* j */
    { 0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00 }, /* k */
    { 0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 }, /* l */
    { 0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00 }, /* m */
    { 0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00 }, /* n */
    { 0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00 }, /* o */
    { 0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F }, /* p */
    { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78 }, /* q */
    { 0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00 }, /* r */
    { 0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00 }, /* s */
    { 0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00 }, /* t */
    { 0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00 }, /* u */
    { 0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00 }, /* v */
    { 0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00 }, /* w */
    { 0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00 }, /* x */
    { 0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F }, /* y */
    { 0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00 }, /* z */
    { 0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00 }, /* { */
    { 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00 }, /* | */
    { 0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00 }, /* } */
    { 0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00 }, /* ~ */
};

/* the 16 standard VGA colours */
static const uint8_t vga_rgb[16][3] = {
    {   0,   0,   0 }, {   0,   0, 170 }, {   0, 170,   0 }, {   0, 170, 170 },
    { 170,   0,   0 }, { 170,   0, 170 }, { 170,  85,   0 }, { 170, 170, 170 },
    {  85,  85,  85 }, {  85,  85, 255 }, {  85, 255,  85 }, {  85, 255, 255 },
    { 255,  85,  85 }, { 255,  85, 255 }, { 255, 255,  85 }, { 255, 255, 255 },
};

/* ---------- state ---------- */

static uint16_t cells[TERM_MAX_ROWS * TERM_MAX_COLS];    /* char | attr << 8, like VGA text memory */
static int term_cols = 80, term_rows = 25;
static int row, col;
static int saved_row, saved_col;
static uint8_t cur_fg = VGA_LIGHT_GREY;
static uint8_t cur_bg = VGA_BLACK;
static int reverse;

static int use_fb;                      /* 0 = VGA text memory, 1 = framebuffer */
static int graphics;                    /* a program owns the screen: don't draw */

static int16_t dirty_lo[TERM_MAX_ROWS]; /* changed column range per row, -1 = clean */
static int16_t dirty_hi[TERM_MAX_ROWS];
static int pending_scroll;              /* framebuffer: pixel rows to move at the next flush */

static uint32_t palette[16];
static int      cursor_drawn, cur_r, cur_c;
static uint16_t cursor_under;           /* what the cell under the drawn cursor looks like */

/* ANSI escape sequence parser state */
static int esc_state;                   /* 0 = none, 1 = after ESC, 2 = inside ESC [ ... */
static int params[8];
static int nparams;
static int have_digits;
static int private_seq;

/* ---------- cells ---------- */

static uint8_t attr(void) {
    if (reverse) return (uint8_t)(((cur_fg & 7) << 4) | (cur_bg & 7));
    return (uint8_t)(((cur_bg & 7) << 4) | (cur_fg & 15));
}

static uint16_t cell(char c) {
    return (uint16_t)(uint8_t)c | ((uint16_t)attr() << 8);
}

static void mark(int r, int c0, int c1) {
    if (dirty_lo[r] < 0 || c0 < dirty_lo[r]) dirty_lo[r] = (int16_t)c0;
    if (dirty_hi[r] < 0 || c1 > dirty_hi[r]) dirty_hi[r] = (int16_t)c1;
}

static void mark_all(void) {
    for (int r = 0; r < term_rows; r++) mark(r, 0, term_cols - 1);
}

static void set_cell(int r, int c, uint16_t v) {
    cells[r * TERM_MAX_COLS + c] = v;
    mark(r, c, c);
}

static void fill_cells(int r, int c0, int c1, uint16_t v) {      /* columns [c0, c1) */
    for (int c = c0; c < c1; c++) cells[r * TERM_MAX_COLS + c] = v;
    if (c1 > c0) mark(r, c0, c1 - 1);
}

static void scroll_up(void) {
    for (int r = 0; r < term_rows - 1; r++) {
        memcpy(&cells[r * TERM_MAX_COLS], &cells[(r + 1) * TERM_MAX_COLS],
               (size_t)term_cols * sizeof(uint16_t));
        dirty_lo[r] = dirty_lo[r + 1];
        dirty_hi[r] = dirty_hi[r + 1];
    }
    for (int c = 0; c < term_cols; c++)
        cells[(term_rows - 1) * TERM_MAX_COLS + c] = cell(' ');
    dirty_lo[term_rows - 1] = 0;
    dirty_hi[term_rows - 1] = (int16_t)(term_cols - 1);

    if (use_fb) pending_scroll++;                /* the pixels are moved once, at flush time */
    else        mark_all();
}

static void newline(void) {
    col = 0;
    if (++row >= term_rows) {
        scroll_up();
        row = term_rows - 1;
    }
}

static void put_plain(char c) {
    switch (c) {
    case '\n':
        newline();
        break;
    case '\r':
        col = 0;
        break;
    case '\t':
        col = (col + TAB_WIDTH) & ~(TAB_WIDTH - 1);
        if (col >= term_cols) newline();
        break;
    case '\b':                                  /* the tty uses this to erase an echoed character */
        if (col > 0) {
            col--;
            set_cell(row, col, cell(' '));
        }
        break;
    default:
        set_cell(row, col, cell(c));
        if (++col >= term_cols) newline();
        break;
    }
}

/* ---------- ANSI escape sequences (the subset editors need) ---------- */

static int param_or(int i, int def) {
    return (i < nparams && params[i] > 0) ? params[i] : def;
}

static int param_raw(int i) {
    return i < nparams ? params[i] : 0;
}

static void sgr(void) {
    static const uint8_t ansi2vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

    if (nparams == 0) {
        cur_fg = VGA_LIGHT_GREY; cur_bg = VGA_BLACK; reverse = 0;
        return;
    }
    for (int i = 0; i < nparams; i++) {
        int p = params[i];
        if (p == 0)        { cur_fg = VGA_LIGHT_GREY; cur_bg = VGA_BLACK; reverse = 0; }
        else if (p == 1)   cur_fg |= 8;
        else if (p == 22)  cur_fg &= 7;
        else if (p == 7)   reverse = 1;
        else if (p == 27)  reverse = 0;
        else if (p >= 30 && p <= 37)   cur_fg = (uint8_t)(ansi2vga[p - 30] | (cur_fg & 8));
        else if (p == 39)  cur_fg = VGA_LIGHT_GREY;
        else if (p >= 40 && p <= 47)   cur_bg = ansi2vga[p - 40];
        else if (p == 49)  cur_bg = VGA_BLACK;
        else if (p >= 90 && p <= 97)   cur_fg = (uint8_t)(ansi2vga[p - 90] | 8);
        else if (p >= 100 && p <= 107) cur_bg = ansi2vga[p - 100];
    }
}

static void csi_dispatch(char f) {
    if (private_seq) return;                    /* ESC [ ? ... : not supported, ignored */

    int n = param_or(0, 1);

    switch (f) {
    case 'A': row = row >= n ? row - n : 0; break;
    case 'B': row += n; if (row >= term_rows) row = term_rows - 1; break;
    case 'C': col += n; if (col >= term_cols) col = term_cols - 1; break;
    case 'D': col = col >= n ? col - n : 0; break;
    case 'H':
    case 'f': {
        int r = param_or(0, 1) - 1;
        int c = param_or(1, 1) - 1;
        row = r >= term_rows ? term_rows - 1 : r;
        col = c >= term_cols ? term_cols - 1 : c;
        break;
    }
    case 'J': {
        int m = param_raw(0);
        uint16_t blank = cell(' ');
        if (m == 0) {
            fill_cells(row, col, term_cols, blank);
            for (int r = row + 1; r < term_rows; r++) fill_cells(r, 0, term_cols, blank);
        } else if (m == 1) {
            for (int r = 0; r < row; r++) fill_cells(r, 0, term_cols, blank);
            fill_cells(row, 0, col + 1, blank);
        } else {
            for (int r = 0; r < term_rows; r++) fill_cells(r, 0, term_cols, blank);
        }
        break;
    }
    case 'K': {
        int m = param_raw(0);
        uint16_t blank = cell(' ');
        if (m == 0)      fill_cells(row, col, term_cols, blank);
        else if (m == 1) fill_cells(row, 0, col + 1, blank);
        else             fill_cells(row, 0, term_cols, blank);
        break;
    }
    case 'm': sgr(); break;
    case 's': saved_row = row; saved_col = col; break;
    case 'u': row = saved_row; col = saved_col; break;
    default: break;
    }
}

static void put_char(char c) {
    if (esc_state == 1) {
        if (c == '[') {
            esc_state = 2;
            nparams = 0;
            have_digits = 0;
            private_seq = 0;
            for (int i = 0; i < 8; i++) params[i] = 0;
        } else {
            esc_state = 0;                      /* unsupported escape: drop it */
        }
        return;
    }

    if (esc_state == 2) {
        if (c >= '0' && c <= '9') {
            if (!have_digits) {
                if (nparams < 8) nparams++;
                have_digits = 1;
            }
            if (nparams > 0) params[nparams - 1] = params[nparams - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (!have_digits && nparams < 8) nparams++;     /* empty parameter */
            have_digits = 0;
        } else if (c == '?') {
            private_seq = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            csi_dispatch(c);
            esc_state = 0;
        } else {
            esc_state = 0;
        }
        return;
    }

    if (c == 0x1B) {
        esc_state = 1;
        return;
    }
    put_plain(c);
}

/* ---------- drawing: VGA text memory ---------- */

static void flush_text(void) {
    for (int r = 0; r < term_rows; r++) {
        if (dirty_lo[r] < 0) continue;
        for (int c = dirty_lo[r]; c <= dirty_hi[r]; c++) {
            uint16_t v = cells[r * TERM_MAX_COLS + c];
            VGA_MEM[r * 80 + c] = (uint16_t)((v & 0xFF00) | latin1_to_vga((uint8_t)(v & 0xFF)));
        }
        dirty_lo[r] = dirty_hi[r] = -1;
    }

    uint16_t pos = (uint16_t)(row * 80 + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* ---------- drawing: framebuffer ---------- */

static inline void store_px(uint8_t *p, uint32_t color, int bytes) {
    if (bytes == 4) {
        *(volatile uint32_t *)p = color;
    } else if (bytes == 2) {
        *(volatile uint16_t *)p = (uint16_t)color;
    } else {
        p[0] = (uint8_t)color;
        p[1] = (uint8_t)(color >> 8);
        p[2] = (uint8_t)(color >> 16);
    }
}

static void draw_value(int r, int c, uint16_t v, int cursor) {
    const struct fb_info *fi = fb_get();
    uint8_t ch = (uint8_t)(v & 0xFF);
    uint8_t at = (uint8_t)(v >> 8);
    uint32_t fg = palette[at & 15];
    uint32_t bg = palette[(at >> 4) & 7];

    if (cursor) { uint32_t t = fg; fg = bg; bg = t; }

    const uint8_t *glyph = (ch >= 32 && ch <= 126) ? font8x8[ch - 32] : latin1_glyph(ch);
    if (!glyph) glyph = font8x8[0];
    int bytes = (int)(fi->bpp / 8);
    uint8_t *dst = fb_base() + (uint32_t)(r * FONT_H) * fi->pitch
                             + (uint32_t)(c * FONT_W) * (uint32_t)bytes;

    for (int y = 0; y < FONT_H; y++) {
        uint8_t bits = glyph[y >> 1];                   /* every font row is drawn twice */
        uint8_t *p = dst + (uint32_t)y * fi->pitch;
        for (int x = 0; x < FONT_W; x++, p += bytes)
            store_px(p, ((bits >> x) & 1) ? fg : bg, bytes);
    }
}

static void copy_forward(void *dst, const void *src, size_t n) {
    size_t words = n / 4, rest = n % 4;
    __asm__ volatile ("cld; rep movsl" : "+D"(dst), "+S"(src), "+c"(words) : : "memory");
    __asm__ volatile ("rep movsb"      : "+D"(dst), "+S"(src), "+c"(rest)  : : "memory");
}

static void pixel_scroll(int rows) {
    const struct fb_info *fi = fb_get();
    uint8_t *base = fb_base();
    uint32_t area = (uint32_t)term_rows * FONT_H;
    uint32_t move = area - (uint32_t)rows * FONT_H;

    copy_forward(base, base + (uint32_t)rows * FONT_H * fi->pitch, (size_t)move * fi->pitch);
}

static void flush_fb(void) {
    if (cursor_drawn) {                                 /* erase the old cursor first */
        draw_value(cur_r, cur_c, cursor_under, 0);
        cursor_drawn = 0;
    }

    if (pending_scroll >= term_rows)      mark_all();
    else if (pending_scroll > 0)          pixel_scroll(pending_scroll);
    pending_scroll = 0;

    for (int r = 0; r < term_rows; r++) {
        if (dirty_lo[r] < 0) continue;
        for (int c = dirty_lo[r]; c <= dirty_hi[r]; c++)
            draw_value(r, c, cells[r * TERM_MAX_COLS + c], 0);
        dirty_lo[r] = dirty_hi[r] = -1;
    }

    cursor_under = cells[row * TERM_MAX_COLS + col];
    draw_value(row, col, cursor_under, 1);
    cursor_drawn = 1;
    cur_r = row;
    cur_c = col;
}

static void flush(void) {
    if (graphics) return;                               /* the cells keep updating, nothing is drawn */
    if (use_fb) flush_fb();
    else        flush_text();
}

/* ---------- public interface ---------- */

static void reset_screen_state(void) {
    for (int r = 0; r < TERM_MAX_ROWS; r++) dirty_lo[r] = dirty_hi[r] = -1;
    for (int r = 0; r < term_rows; r++)
        for (int c = 0; c < term_cols; c++)
            cells[r * TERM_MAX_COLS + c] = cell(' ');
    row = col = 0;
    saved_row = saved_col = 0;
    pending_scroll = 0;
    cursor_drawn = 0;
    mark_all();
}

void terminal_init(void) {
    use_fb = 0;
    graphics = 0;
    esc_state = 0;
    reverse = 0;
    cur_fg = VGA_LIGHT_GREY;
    cur_bg = VGA_BLACK;
    term_cols = 80;
    term_rows = 25;
    reset_screen_state();
    flush();
}

void terminal_attach_fb(void) {
    const struct fb_info *fi = fb_get();
    if (!fi->present) return;

    term_cols = (int)(fi->width / FONT_W);
    term_rows = (int)(fi->height / FONT_H);
    if (term_cols > TERM_MAX_COLS) term_cols = TERM_MAX_COLS;
    if (term_rows > TERM_MAX_ROWS) term_rows = TERM_MAX_ROWS;
    if (term_cols < 1) term_cols = 1;
    if (term_rows < 1) term_rows = 1;

    for (int i = 0; i < 16; i++)
        palette[i] = fb_color(vga_rgb[i][0], vga_rgb[i][1], vga_rgb[i][2]);

    use_fb = 1;
    memset(fb_base(), 0, fi->size);                     /* black everywhere, margins included */
    reset_screen_state();
    flush();
}

void terminal_set_graphics(int on) {
    graphics = (on != 0);
    if (graphics) return;

    if (use_fb) memset(fb_base(), 0, fb_get()->size);   /* the program's picture is gone */
    pending_scroll = 0;
    cursor_drawn = 0;
    mark_all();
    flush();
}

void terminal_get_size(int *rows, int *cols) {
    *rows = term_rows;
    *cols = term_cols;
}

void terminal_setcolor(enum vga_color fg, enum vga_color bg) {
    cur_fg = (uint8_t)fg;
    cur_bg = (uint8_t)bg;
    reverse = 0;
}

void terminal_clear(void) {
    uint16_t blank = cell(' ');
    for (int r = 0; r < term_rows; r++) fill_cells(r, 0, term_cols, blank);
    row = col = 0;
    pending_scroll = 0;
    flush();
}

void terminal_putc(char c) {
    put_char(c);
    flush();
}

void terminal_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) put_char(s[i]);
    flush();                                            /* once per write, not per character */
}

void terminal_puts(const char *s) {
    terminal_write(s, strlen(s));
}