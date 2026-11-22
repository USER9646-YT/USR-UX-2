#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "hostname.h"

#define HOSTNAME_MAX 64

static char name[HOSTNAME_MAX + 1] = "localhost";

const char *hostname_get(void) {
    return name;
}

int hostname_set(const char *src, uint32_t len) {
    if (len == 0 || len > HOSTNAME_MAX) return -EINVAL;

    for (uint32_t i = 0; i < len; i++) {                // letters, digits, '-', '.' and '_' only
        char c = src[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'))
            return -EINVAL;
    }

    memcpy(name, src, len);
    name[len] = '\0';
    return 0;
}