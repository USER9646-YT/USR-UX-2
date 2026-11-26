#include <string.h>
#include <libgen.h>

#define WEAK __attribute__((weak))

WEAK char *dirname(char *path) {
    static char dot[] = ".";
    char *end;

    if (!path || !*path) return dot;
    end = path + strlen(path) - 1;
    while (end > path && *end == '/') end--;            // trailing slashes
    while (end > path && *end != '/') end--;            // the last component
    if (end == path) {                                  // no directory part, or only the root
        if (*path == '/') { path[1] = '\0'; return path; }
        return dot;
    }
    while (end > path && *end == '/') end--;            // slashes in front of it
    end[1] = '\0';
    return path;
}

WEAK char *basename(char *path) {
    static char dot[] = ".";
    char *end, *p;

    if (!path || !*path) return dot;
    end = path + strlen(path);
    while (end > path + 1 && end[-1] == '/') *--end = '\0';     // trailing slashes (keep a lone "/")
    if (end == path + 1 && *path == '/') return path;
    p = strrchr(path, '/');
    return p ? p + 1 : path;
}