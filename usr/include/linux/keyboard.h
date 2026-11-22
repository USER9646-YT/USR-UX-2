#ifndef _LINUX_KEYBOARD_H
#define _LINUX_KEYBOARD_H

// A subset of the Linux keymap definitions, enough for programs that only decode key
// values. This kernel delivers translated characters (K_XLATE) and has no key maps of
// its own, so the keymap ioctls are not available.

#define KG_SHIFT     0
#define KG_ALTGR     1
#define KG_CTRL      2
#define KG_ALT       3
#define KG_SHIFTL    4
#define KG_SHIFTR    5
#define KG_CTRLL     6
#define KG_CTRLR     7
#define KG_CAPSSHIFT 8

#define NR_SHIFT     9

#define NR_KEYS      128
#define MAX_NR_KEYMAPS 256
#define MAX_NR_FUNC  256

#define KT_LATIN  0
#define KT_FN     1
#define KT_SPEC   2
#define KT_PAD    3
#define KT_DEAD   4
#define KT_CONS   5
#define KT_CUR    6
#define KT_SHIFT  7
#define KT_META   8
#define KT_ASCII  9
#define KT_LOCK   10
#define KT_LETTER 11
#define KT_SLOCK  12

#define K(t, v)  (((t) << 8) | (v))
#define KTYP(x)  ((x) >> 8)
#define KVAL(x)  ((x) & 0xff)

#define K_F1     K(KT_FN, 0)
#define K_F2     K(KT_FN, 1)
#define K_F3     K(KT_FN, 2)
#define K_F4     K(KT_FN, 3)
#define K_F5     K(KT_FN, 4)
#define K_F6     K(KT_FN, 5)
#define K_F7     K(KT_FN, 6)
#define K_F8     K(KT_FN, 7)
#define K_F9     K(KT_FN, 8)
#define K_F10    K(KT_FN, 9)
#define K_F11    K(KT_FN, 10)
#define K_F12    K(KT_FN, 11)
#define K_FIND   K(KT_FN, 20)
#define K_INSERT K(KT_FN, 21)
#define K_REMOVE K(KT_FN, 22)
#define K_SELECT K(KT_FN, 23)
#define K_PGUP   K(KT_FN, 24)
#define K_PGDN   K(KT_FN, 25)

#define K_HOLE   K(KT_SPEC, 0)
#define K_ENTER  K(KT_SPEC, 1)
#define K_CAPS   K(KT_SPEC, 7)
#define K_NUM    K(KT_SPEC, 8)

#define K_DOWN   K(KT_CUR, 0)
#define K_LEFT   K(KT_CUR, 1)
#define K_RIGHT  K(KT_CUR, 2)
#define K_UP     K(KT_CUR, 3)

#define K_SHIFT  K(KT_SHIFT, KG_SHIFT)
#define K_ALTGR  K(KT_SHIFT, KG_ALTGR)
#define K_CTRL   K(KT_SHIFT, KG_CTRL)
#define K_ALT    K(KT_SHIFT, KG_ALT)

#endif /* _LINUX_KEYBOARD_H */