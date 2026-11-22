#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

// Prints a white-on-red message and stops the machine obviously. What fucking else is this shit supposed to do? Make coffee?
void kpanic(const char *fmt, ...) __attribute__((noreturn));

#endif