#include <stdint.h>
#include <stddef.h>
#include "kbdmenu.h"
#include "keymap.h"
#include "keyboard.h"
#include "multiboot.h"
#include "terminal.h"
#include "kprintf.h"
#include "rtc.h"

#define COUNTDOWN_SECONDS 5

// Looks for "kbd=<value>" on the kernel command line; returns the layout index or -1 
static int cmdline_layout(const struct multiboot_info *mbi) {
    if (!(mbi->flags & MBI_FLAG_CMDLINE) || !mbi->cmdline) return -1;

    const char *p = (const char *)(uintptr_t)mbi->cmdline;
    for (; *p; p++) {
        if (p != (const char *)(uintptr_t)mbi->cmdline && p[-1] != ' ') continue;
        if (p[0] == 'k' && p[1] == 'b' && p[2] == 'd' && p[3] == '=') {
            char name[16];
            int n = 0;
            for (const char *v = p + 4; *v && *v != ' ' && n < 15; v++) name[n++] = *v;
            name[n] = '\0';
            return keymap_find(name);
        }
    }
    return -1;
}

static void draw(int sel, int seconds_left) {
    terminal_clear();
    kprintf("\033[H");
    kprintf("Choose your keyboard layout\n\n");

    for (int i = 0; i < keymap_count(); i++) {
        if (i == sel) kprintf("\033[7m");
        kprintf("  %c %d) %s  ", i == sel ? '>' : ' ', i + 1, keymap_get(i)->title);
        if (i == sel) kprintf("\033[0m");
        kprintf("\n");
    }

    kprintf("\nUp/Down or the number keys to choose, Enter to confirm.\n");
    if (seconds_left >= 0)
        kprintf("Continuing with \"%s\" in %d s (press any key to stop the countdown).\n",
                keymap_get(sel)->title, seconds_left);
    else
        kprintf("\n");
}

void kbdmenu_run(const struct multiboot_info *mbi) {
    int chosen = cmdline_layout(mbi);

    if (chosen >= 0) {                              // kbd=de on the command line: no menu 
        keyboard_set_layout(chosen);
        kprintf("keyboard: %s (from the command line)\n", keymap_get(chosen)->title);
        return;
    }

    while (keyboard_poll_scancode() >= 0) ;         // drop whatever is already waiting 

    int sel = 0, ext = 0, counting = 1, last_shown = -2;
    uint32_t start = rtc_read_epoch();
    uint32_t spin = 0;

    for (;;) {
        int sc = keyboard_poll_scancode();

        if (sc < 0) {
            if (counting && (++spin & 0xFFF) == 0) {
                uint32_t elapsed = rtc_read_epoch() - start;
                if (elapsed >= COUNTDOWN_SECONDS) break;

                int left = (int)(COUNTDOWN_SECONDS - elapsed);
                if (left != last_shown) { last_shown = left; draw(sel, left); }
            } else if (last_shown == -2) {
                last_shown = -1;
                draw(sel, COUNTDOWN_SECONDS);       // first picture 
            }
            __asm__ volatile ("pause");
            continue;
        }

        if (sc == 0xE0) { ext = 1; continue; }

        int release = sc & 0x80;
        int code = sc & 0x7F;
        int was_ext = ext;
        ext = 0;
        if (release) continue;

        counting = 0;                               // any key press stops the countdown 
        int n = keymap_count();

        if (was_ext && code == 0x48)      sel = (sel + n - 1) % n;          // up 
        else if (was_ext && code == 0x50) sel = (sel + 1) % n;              // down 
        else if (code == 0x1C)            break;                            // enter (also keypad enter) 
        else if (!was_ext && code >= 0x02 && code < 0x02 + n) sel = code - 0x02;   // 1, 2, ... 

        draw(sel, -1);
    }

    keyboard_set_layout(sel);
    terminal_clear();
    kprintf("\033[H");
    kprintf("keyboard: %s\n", keymap_get(sel)->title);
}