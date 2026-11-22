#include <stdint.h>
#include "power.h"
#include "io.h"
#include "kprintf.h"

static void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

void power_reboot(void) {
    __asm__ volatile ("cli");
    kprintf("\nRestarting system.\n");

    // pulse the CPU reset line through the keyboard controller
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) break;
    outb(0x64, 0xFE);
    for (volatile int i = 0; i < 1000000; i++) ;

    // still alive: force a triple fault with an empty IDT
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) empty = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(empty));
    halt_forever();
}

void power_off(void) {
    __asm__ volatile ("cli");
    kprintf("\nPower down.\n");

    outw(0x604, 0x2000);        // QEMU (and some chipsets)
    outw(0xB004, 0x2000);       // Bochs, older QEMU
    outw(0x4004, 0x3400);       // VirtualBox

    kprintf("It is now safe to turn off your computer.\n");
    halt_forever();
}

void power_halt(void) {
    __asm__ volatile ("cli");
    kprintf("\nSystem halted.\n");
    halt_forever();
}