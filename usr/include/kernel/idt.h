#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

#include <stdint.h>

// A stack layout when isr_handler() is called (matches isr_common in isr.S)
struct registers {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pusha order
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;            // pushed by CPU
};

typedef void (*isr_handler_t)(struct registers *regs);

void idt_init(void);
void idt_set_gate(uint8_t n, uint32_t handler, uint16_t sel, uint8_t flags);
void isr_register_handler(uint8_t n, isr_handler_t handler);
void idt_debug(uint8_t n);

#endif