#include <stdint.h>
#include <stddef.h>
#include "keymap.h"

// Latin-1 codes for the characters ASCII lacks
#define AE_LO 0xE4      // a umlaut
#define AE_UP 0xC4
#define OE_LO 0xF6      // o umlaut
#define OE_UP 0xD6
#define UE_LO 0xFC      // u umlaut
#define UE_UP 0xDC
#define SZ    0xDF      // sharp s
#define PARA  0xA7      // section sign
#define DEG   0xB0      // degree sign
#define SUP2  0xB2
#define SUP3  0xB3
#define ACUTE 0xB4
#define MICRO 0xB5

#define K(sc, n, s, a) { (sc), (uint8_t)(n), (uint8_t)(s), (uint8_t)(a) }

// ---------- US English (QWERTY) ---------- //

static const struct keymap_key us_keys[] = {
    K(0x01, 27, 27, 0),
    K(0x02, '1', '!', 0), K(0x03, '2', '@', 0), K(0x04, '3', '#', 0), K(0x05, '4', '$', 0),
    K(0x06, '5', '%', 0), K(0x07, '6', '^', 0), K(0x08, '7', '&', 0), K(0x09, '8', '*', 0),
    K(0x0A, '9', '(', 0), K(0x0B, '0', ')', 0), K(0x0C, '-', '_', 0), K(0x0D, '=', '+', 0),
    K(0x0E, '\b', '\b', 0), K(0x0F, '\t', '\t', 0),
    K(0x10, 'q', 'Q', 0), K(0x11, 'w', 'W', 0), K(0x12, 'e', 'E', 0), K(0x13, 'r', 'R', 0),
    K(0x14, 't', 'T', 0), K(0x15, 'y', 'Y', 0), K(0x16, 'u', 'U', 0), K(0x17, 'i', 'I', 0),
    K(0x18, 'o', 'O', 0), K(0x19, 'p', 'P', 0), K(0x1A, '[', '{', 0), K(0x1B, ']', '}', 0),
    K(0x1C, '\n', '\n', 0),
    K(0x1E, 'a', 'A', 0), K(0x1F, 's', 'S', 0), K(0x20, 'd', 'D', 0), K(0x21, 'f', 'F', 0),
    K(0x22, 'g', 'G', 0), K(0x23, 'h', 'H', 0), K(0x24, 'j', 'J', 0), K(0x25, 'k', 'K', 0),
    K(0x26, 'l', 'L', 0), K(0x27, ';', ':', 0), K(0x28, '\'', '"', 0), K(0x29, '`', '~', 0),
    K(0x2B, '\\', '|', 0),
    K(0x2C, 'z', 'Z', 0), K(0x2D, 'x', 'X', 0), K(0x2E, 'c', 'C', 0), K(0x2F, 'v', 'V', 0),
    K(0x30, 'b', 'B', 0), K(0x31, 'n', 'N', 0), K(0x32, 'm', 'M', 0), K(0x33, ',', '<', 0),
    K(0x34, '.', '>', 0), K(0x35, '/', '?', 0),
    K(0x37, '*', '*', 0), K(0x39, ' ', ' ', 0),
    K(0x56, '<', '>', '|'),                    // the extra key on ISO keyboards
};

// ---------- German (QWERTZ). The dead keys (^ and the acute accent) type their character directly. ---------- //

static const struct keymap_key de_keys[] = {
    K(0x01, 27, 27, 0),
    K(0x02, '1', '!', 0),    K(0x03, '2', '"', SUP2), K(0x04, '3', PARA, SUP3), K(0x05, '4', '$', 0),
    K(0x06, '5', '%', 0),    K(0x07, '6', '&', 0),    K(0x08, '7', '/', '{'),  K(0x09, '8', '(', '['),
    K(0x0A, '9', ')', ']'),  K(0x0B, '0', '=', '}'),  K(0x0C, SZ, '?', '\\'),  K(0x0D, ACUTE, '`', 0),
    K(0x0E, '\b', '\b', 0),  K(0x0F, '\t', '\t', 0),
    K(0x10, 'q', 'Q', '@'),  K(0x11, 'w', 'W', 0),    K(0x12, 'e', 'E', 0),    K(0x13, 'r', 'R', 0),
    K(0x14, 't', 'T', 0),    K(0x15, 'z', 'Z', 0),    K(0x16, 'u', 'U', 0),    K(0x17, 'i', 'I', 0),
    K(0x18, 'o', 'O', 0),    K(0x19, 'p', 'P', 0),    K(0x1A, UE_LO, UE_UP, 0), K(0x1B, '+', '*', '~'),
    K(0x1C, '\n', '\n', 0),
    K(0x1E, 'a', 'A', 0),    K(0x1F, 's', 'S', 0),    K(0x20, 'd', 'D', 0),    K(0x21, 'f', 'F', 0),
    K(0x22, 'g', 'G', 0),    K(0x23, 'h', 'H', 0),    K(0x24, 'j', 'J', 0),    K(0x25, 'k', 'K', 0),
    K(0x26, 'l', 'L', 0),    K(0x27, OE_LO, OE_UP, 0), K(0x28, AE_LO, AE_UP, 0), K(0x29, '^', DEG, 0),
    K(0x2B, '#', '\'', 0),
    K(0x2C, 'y', 'Y', 0),    K(0x2D, 'x', 'X', 0),    K(0x2E, 'c', 'C', 0),    K(0x2F, 'v', 'V', 0),
    K(0x30, 'b', 'B', 0),    K(0x31, 'n', 'N', 0),    K(0x32, 'm', 'M', MICRO), K(0x33, ',', ';', 0),
    K(0x34, '.', ':', 0),    K(0x35, '-', '_', 0),
    K(0x37, '*', '*', 0),    K(0x39, ' ', ' ', 0),
    K(0x56, '<', '>', '|'),
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const struct keymap maps[] = {
    { "us", "US English",                us_keys, COUNT(us_keys) },
    { "de", "Deutsch (German, QWERTZ)",  de_keys, COUNT(de_keys) },
};

int keymap_count(void) {
    return COUNT(maps);
}

const struct keymap *keymap_get(int index) {
    return (index >= 0 && index < COUNT(maps)) ? &maps[index] : NULL;
}

int keymap_find(const char *name) {
    for (int i = 0; i < COUNT(maps); i++) {
        const char *a = maps[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

uint8_t keymap_char(const struct keymap *m, uint8_t scancode, int shift, int altgr) {
    for (int i = 0; i < m->nkeys; i++) {
        const struct keymap_key *k = &m->keys[i];
        if (k->scancode != scancode) continue;
        if (altgr && k->altgr) return k->altgr;
        return shift ? k->shifted : k->normal;
    }
    return 0;
}