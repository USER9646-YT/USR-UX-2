#ifndef KERNEL_KPRINTF_H
#define KERNEL_KPRINTF_H

#include <stdarg.h>

int kprintf(const char *fmt, ...);
int kvprintf(const char *fmt, va_list ap);

#endif