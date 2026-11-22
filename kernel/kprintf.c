#include <stdint.h>
#include <stddef.h>
#include "kprintf.h"
#include "terminal.h"

static int print_num(uint32_t v, unsigned base, int upper,
                     int neg, int width, char pad) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *digits = upper ? up : lo;

    char buf[32];
    int i = 0, n = 0;

    if (v == 0) buf[i++] = '0';
    while (v) {
        buf[i++] = digits[v % base];
        v /= base;
    }

    int total = i + neg;

    if (neg && pad == '0') { terminal_putc('-'); n++; }
    for (; total < width; total++) { terminal_putc(pad); n++; }
    if (neg && pad != '0') { terminal_putc('-'); n++; }

    while (i--) { terminal_putc(buf[i]); n++; }
    return n;
}

int kvprintf(const char *fmt, va_list ap) {
    int count = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            terminal_putc(*fmt);
            count++;
            continue;
        }

        fmt++;                       /* skip '%' */

        char pad = ' ';
        int width = 0;

        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            terminal_putc((char)va_arg(ap, int));
            count++;
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) { terminal_putc(*s++); count++; }
            break;
        }
        case 'd':
        case 'i': {
            int32_t x = va_arg(ap, int32_t);
            uint32_t u = (x < 0) ? 0u - (uint32_t)x : (uint32_t)x;
            count += print_num(u, 10, 0, x < 0, width, pad);
            break;
        }
        case 'u':
            count += print_num(va_arg(ap, uint32_t), 10, 0, 0, width, pad);
            break;
        case 'x':
            count += print_num(va_arg(ap, uint32_t), 16, 0, 0, width, pad);
            break;
        case 'X':
            count += print_num(va_arg(ap, uint32_t), 16, 1, 0, width, pad);
            break;
        case 'o':
            count += print_num(va_arg(ap, uint32_t), 8, 0, 0, width, pad);
            break;
        case 'p':
            terminal_putc('0'); terminal_putc('x');
            count += 2;
            count += print_num((uint32_t)(uintptr_t)va_arg(ap, void *),
                               16, 0, 0, 8, '0');
            break;
        case '%':
            terminal_putc('%');
            count++;
            break;
        case '\0':
            return count;            /* format ended after '%' */
        default:
            terminal_putc('%');
            terminal_putc(*fmt);
            count += 2;
            break;
        }
    }
    return count;
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}