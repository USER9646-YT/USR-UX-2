#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", file, line, func, expr);
    fflush(stderr);
    abort();
}

// only the "C" locale exists
char *setlocale(int category, const char *locale) {
    (void)category;
    if (!locale || !*locale || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0)
        return "C";
    return NULL;
}