#ifndef KERNEL_KEYMAP_H
#define KERNEL_KEYMAP_H

#include <stdint.h>

// Keyboard layouts. Characters are single bytes in ISO 8859-1 (Latin-1), so a
// German keyboard types a-umlaut as 0xE4, and plain ASCII is unchanged.

struct keymap_key {
    uint8_t scancode;           // PC/AT set 1 scancode (the keyboard controller's translated form)
    uint8_t normal;
    uint8_t shifted;
    uint8_t altgr;              // 0 = AltGr does nothing on this key
};

struct keymap {
    const char              *name;      // short name, as used by kbd=de on the kernel command line
    const char              *title;     // shown in the menu
    const struct keymap_key *keys;
    int                      nkeys;
};

int                  keymap_count(void);
const struct keymap *keymap_get(int index);          // NULL if out of range
int                  keymap_find(const char *name);  // index, or -1

// The character a key produces, or 0 if it produces none
uint8_t keymap_char(const struct keymap *m, uint8_t scancode, int shift, int altgr);

#endif