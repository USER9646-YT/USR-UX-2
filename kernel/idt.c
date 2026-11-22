#include <stdint.h>
#include <string.h>
#include "idt.h"
#include "kprintf.h"
#include "terminal.h"
#include "pic.h"
#include "proc.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;
static isr_handler_t    handlers[IDT_ENTRIES];

extern uint32_t isr_stub_table[];
extern void isr128(void);

static const char *exception_names[32] = {
    "div error",                 "debug",
    "non-maskable interrupt",    "breakpoint",
    "overflow",                  "bound range exceeded",
    "invalid opcode",            "device not available",
    "double fault",              "coprocessor segment overrun",
    "invalid TSS",               "segment not present",
    "stack-segment fault",       "general protection fault",
    "page fault",                "reserved",
    "x87 floating-point error",  "alignment check",
    ",achine check",             "SIMD floating-point error",
    "virtualization exception",  "control protection exception",
    "reserved", "reserved", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved",
    "security exception",        "reserved"
};

void idt_set_gate(uint8_t n, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[n].base_low  = handler & 0xFFFF;
    idt[n].base_high = (handler >> 16) & 0xFFFF;
    idt[n].sel       = sel;
    idt[n].zero      = 0;
    idt[n].flags     = flags;
}

extern void isr128(void);

void idt_init(void) {
    memset(idt, 0, sizeof(idt));

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)(uintptr_t)&idt;

    for (int i = 0; i < 48; i++)
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x08, 0x8E);

    // 0xEE = present, ring 3 may call it, 32-bit interrupt gate
    idt_set_gate(128, (uint32_t)(uintptr_t)isr128, 0x08, 0xEE);

    __asm__ volatile ("lidt %0" : : "m"(idtp));
}

void isr_register_handler(uint8_t n, isr_handler_t handler) {
    handlers[n] = handler;
}

static void panic_exception(struct registers *r) {
    uint32_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\npanic: %s (int %u, err=%08x)\n",
            r->int_no < 32 ? exception_names[r->int_no] : "Unknown",
            r->int_no, r->err_code);
    kprintf("EIP=%08x CS=%04x EFLAGS=%08x\n", r->eip, r->cs, r->eflags);
    kprintf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n",
            r->esi, r->edi, r->ebp, r->esp);
    if (r->int_no == 14)
        kprintf("CR2=%08x (faulting address)\n", cr2);
    kprintf("Looks like you fucked some shit up in here...\n");
    kprintf("System halted.\n");

    for (;;) __asm__ volatile ("cli; hlt");
}

static void user_fault(struct registers *r) {
    kprintf("\n[pid %d: %s at EIP=%08x] killed\n", current->pid,
            r->int_no < 32 ? exception_names[r->int_no] : "fault", r->eip);
    proc_exit(128 + (int)r->int_no);
}

// Called from isr_common in isr.S
void isr_handler(struct registers *r) {
    // Acknowledge hardware interrupts first: a handler may switch to another
    //   process and not come back for a while.
    if (r->int_no >= 32 && r->int_no < 48)
        pic_eoi((uint8_t)(r->int_no - 32));

    if (handlers[r->int_no]) {
        handlers[r->int_no](r);
    } else if (r->int_no < 32) {
        if (r->cs & 3) user_fault(r);       // a user program misbehaved
        panic_exception(r);                 // a kernel bug
    }
}