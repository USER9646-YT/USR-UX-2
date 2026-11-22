#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <stdint.h>
#include <stddef.h>

#define TERM_MAX_COLS 160
#define TERM_MAX_ROWS 64

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};

void terminal_init(void);                       // VGA text mode, 80x25
void terminal_attach_fb(void);                  // switch the console to the framebuffer
void terminal_set_graphics(int on);             // 1: a program owns the screen, stop drawing
void terminal_get_size(int *rows, int *cols);

void terminal_setcolor(enum vga_color fg, enum vga_color bg);
void terminal_putc(char c);
void terminal_write(const char *s, size_t n);
void terminal_puts(const char *s);
void terminal_clear(void);

#endif