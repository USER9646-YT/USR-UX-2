#ifndef KERNEL_LATIN1_H
#define KERNEL_LATIN1_H

#include <stdint.h>

// 8x8 glyph (same layout as the ASCII font in terminal.c) for a Latin-1 character
// the keyboard layouts can produce, or NULL if there is none.
const uint8_t *latin1_glyph(uint8_t ch);

// The byte to store in VGA text memory so that the hardware font (code page 437)
// shows this Latin-1 character. Characters without a counterpart pass through.
uint8_t latin1_to_vga(uint8_t ch);

#endif