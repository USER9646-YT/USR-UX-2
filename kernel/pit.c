#include "pit.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "proc.h"

#define PIT_HZ_BASE 1193182u

static volatile uint32_t ticks;
static uint32_t freq;

static void pit_handler(struct registers *r) {
    ticks++;
    proc_tick(ticks);                       // wake processes whose sleep has ended

    if ((r->cs & 3) == 3) {                 // interrupted user mode, safe to switch or kill
        if (current->killed) proc_exit(128 + current->killed);
        yield();
        if (current->killed) proc_exit(128 + current->killed);
    }
}

void pit_init(uint32_t hz) {
    freq = hz;
    uint32_t div = PIT_HZ_BASE / hz;

    outb(0x43, 0x36);                           // channel 0, lobyte/hibyte, square wave
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));

    isr_register_handler(32, pit_handler);
    pic_unmask(0);
}

uint32_t pit_ticks(void) {
    return ticks;
}

uint32_t pit_freq(void) {
    return freq;
}