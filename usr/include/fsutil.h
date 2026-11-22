#ifndef FSUTIL_H
#define FSUTIL_H

#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"

#define PATH_BUF 256
#define MAX_DEPTH 8

static inline int fs_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static inline const char *fs_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' && p[1]) base = p + 1;
    return base;
}

// dst = dir + "/" + name; returns 0, or -1 if it doesn't fit
static inline int fs_join(char *dst, unsigned int max, const char *dir, const char *name) {
    unsigned int n = 0;

    for (const char *p = dir; *p; p++) {
        if (n + 1 >= max) return -1;
        dst[n++] = *p;
    }
    if (n > 0 && dst[n - 1] != '/') {
        if (n + 1 >= max) return -1;
        dst[n++] = '/';
    }
    for (const char *p = name; *p; p++) {
        if (n + 1 >= max) return -1;
        dst[n++] = *p;
    }
    dst[n] = '\0';
    return 0;
}

static inline int fs_copy_file(const char *prog, const char *src, const char *dst) {
    char buf[1024];
    struct stat st;
    int rc = 0, n;

    int in = open(src, O_RDONLY);
    if (in < 0) { eprint(prog, src, in); return -1; }
    if (stat(src, &st) < 0) st.st_mode = 0644;

    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) { close(in); eprint(prog, dst, out); return -1; }

    while ((n = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, (unsigned int)n) != n) {
            eprint(prog, dst, -28);
            rc = -1;
            break;
        }
    }
    close(in);
    close(out);

    chmod(dst, st.st_mode & 07777);          // keep the permission bits (best effort)
    return rc;
}

static inline int fs_copy_tree(const char *prog, const char *src, const char *dst, int depth) {
    struct stat st;
    int r = stat(src, &st);
    if (r < 0) { eprint(prog, src, r); return -1; }

    if (!S_ISDIR(st.st_mode)) return fs_copy_file(prog, src, dst);

    if (depth >= MAX_DEPTH) { eprint(prog, src, -36); return -1; }

    r = mkdir(dst, 0755);
    if (r < 0 && r != -17) { eprint(prog, dst, r); return -1; }

    int fd = open(src, O_RDONLY);
    if (fd < 0) { eprint(prog, src, fd); return -1; }

    struct dirent de;
    int rc = 0;
    while (readdir(fd, &de) > 0) {
        char s2[PATH_BUF], d2[PATH_BUF];

        if (streq(de.d_name, ".") || streq(de.d_name, "..")) continue;
        if (fs_join(s2, sizeof(s2), src, de.d_name) < 0 ||
            fs_join(d2, sizeof(d2), dst, de.d_name) < 0) {
            eprint(prog, de.d_name, -36);
            rc = -1;
            continue;
        }
        if (fs_copy_tree(prog, s2, d2, depth + 1) < 0) rc = -1;
    }
    close(fd);
    return rc;
}

// Removes a file, or (with `recursive`) a whole directory tree
static inline int fs_remove_tree(const char *prog, const char *path, int recursive, int depth) {
    struct stat st;
    int r = stat(path, &st);
    if (r < 0) { eprint(prog, path, r); return -1; }

    if (!S_ISDIR(st.st_mode)) {
        r = unlink(path);
        if (r < 0) { eprint(prog, path, r); return -1; }
        return 0;
    }

    if (!recursive) { eprint(prog, path, -21); return -1; }
    if (depth >= MAX_DEPTH) { eprint(prog, path, -36); return -1; }

    // Removing entries shifts the directory's indices, so reopen it after every removal
    for (;;) {
        char child[PATH_BUF];
        struct dirent de;
        int found = 0;

        int fd = open(path, O_RDONLY);
        if (fd < 0) { eprint(prog, path, fd); return -1; }

        while (readdir(fd, &de) > 0) {
            if (streq(de.d_name, ".") || streq(de.d_name, "..")) continue;
            if (fs_join(child, sizeof(child), path, de.d_name) < 0) {
                close(fd);
                eprint(prog, de.d_name, -36);
                return -1;
            }
            found = 1;
            break;
        }
        close(fd);

        if (!found) break;
        if (fs_remove_tree(prog, child, 1, depth + 1) < 0) return -1;   // stop on failure
    }

    r = rmdir(path);
    if (r < 0) { eprint(prog, path, r); return -1; }
    return 0;
}

#endif