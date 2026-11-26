#include <syslog.h>

#define WEAK __attribute__((weak))

static int mask = 0xFF;

WEAK void openlog(const char *ident, int option, int facility) { (void)ident; (void)option; (void)facility; }
WEAK void syslog(int pri, const char *fmt, ...)                 { (void)pri; (void)fmt; }
WEAK void vsyslog(int pri, const char *fmt, va_list ap)         { (void)pri; (void)fmt; (void)ap; }
WEAK void closelog(void)                                        { }
WEAK int  setlogmask(int m)                                     { int old = mask; if (m) mask = m; return old; }