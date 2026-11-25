#include "glob.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Portable glob subset that only uses POSIX directory and file APIs.
// Why? Because I want to port hush, is that a good enough of a fucking answer?

static int has_magic(const char *s, int flags)
{
    int esc = 0;
    for (; *s; ++s) {
        if (!(flags & GLOB_NOESCAPE) && !esc && *s == '\\') {
            esc = 1;
            continue;
        }
        if (!esc && (*s == '*' || *s == '?' || *s == '['))
            return 1;
        esc = 0;
    }
    return 0;
}

static int class_match(const char **pp, unsigned char c)
{
    const char *p = *pp;
    int invert = 0, matched = 0, have = 0;

    if (*p == '!' || *p == '^') {
        invert = 1;
        ++p;
    }
    if (*p == ']') {
        have = 1;
        matched = (c == ']');
        ++p;
    }
    while (*p && *p != ']') {
        unsigned char lo;
        if (*p == '\\' && p[1]) ++p;
        lo = (unsigned char)*p++;
        have = 1;
        if (*p == '-' && p[1] && p[1] != ']') {
            unsigned char hi;
            ++p;
            if (*p == '\\' && p[1]) ++p;
            hi = (unsigned char)*p++;
            if (lo <= c && c <= hi) matched = 1;
        } else {
            if (c == lo) matched = 1;
        }
    }
    if (*p != ']' || !have) {
        *pp = p; // caller treats malformed '[' literally
        return -1;
    }
    *pp = p + 1;
    return invert ? !matched : matched;
}

static int wildmatch(const char *p, const char *s, int flags)
{
    while (*p) {
        if (!(flags & GLOB_NOESCAPE) && *p == '\\' && p[1]) {
            ++p;
            if (*p++ != *s++) return 0;
        } else if (*p == '*') {
            while (*p == '*') ++p;
            if (!*p) return 1;
            do {
                if (wildmatch(p, s, flags)) return 1;
            } while (*s++);
            return 0;
        } else if (*p == '?') {
            if (!*s) return 0;
            ++p; ++s;
        } else if (*p == '[') {
            const char *q = p + 1;
            if (!*s)
                return 0;
            {
                int cm = class_match(&q, (unsigned char)*s);
                if (cm < 0) {
                    if (*s != '[') return 0;
                    ++p; ++s;
                } else if (!cm) {
                    return 0;
                } else {
                    p = q; ++s;
                }
            }
        } else {
            if (*p++ != *s++) return 0;
        }
    }
    return *s == '\0';
}

static char *join_path(const char *base, const char *name)
{
    size_t a = strlen(base), b = strlen(name);
    int slash = a && base[a - 1] != '/';
    char *r = (char *)malloc(a + (size_t)slash + b + 1);
    if (!r) return NULL;
    memcpy(r, base, a);
    if (slash) r[a++] = '/';
    memcpy(r + a, name, b + 1);
    return r;
}

static int add_result(glob_t *g, const char *path, int flags)
{
    struct stat st;
    char *copy;
    char **nv;
    size_t n = g->gl_pathc;
    size_t extra = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;

    copy = strdup(path);
    if (!copy) return GLOB_NOSPACE;
    if ((flags & GLOB_MARK) && lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(copy);
        if (!len || copy[len - 1] != '/') {
            char *tmp = (char *)realloc(copy, len + 2);
            if (!tmp) { free(copy); return GLOB_NOSPACE; }
            copy = tmp;
            copy[len] = '/';
            copy[len + 1] = '\0';
        }
    }
    nv = (char **)realloc(g->gl_pathv,
            (extra + n + 2) * sizeof(*nv));
    if (!nv) { free(copy); return GLOB_NOSPACE; }
    g->gl_pathv = nv;
    if (!n && extra) {
        size_t i;
        for (i = 0; i < extra; ++i) g->gl_pathv[i] = NULL;
    }
    g->gl_pathv[extra + n] = copy;
    g->gl_pathv[extra + n + 1] = NULL;
    g->gl_pathc++;
    return 0;
}

static int compare_strings(const void *a, const void *b)
{
    const char *const *x = (const char *const *)a;
    const char *const *y = (const char *const *)b;
    return strcmp(*x, *y);
}

static int walk_pattern(const char *prefix, const char *rest, int flags,
                        int (*errfunc)(const char *, int), glob_t *g)
{
    const char *slash = strchr(rest, '/');
    size_t seglen = slash ? (size_t)(slash - rest) : strlen(rest);
    char *segment = (char *)malloc(seglen + 1);
    const char *tail = slash ? slash + 1 : NULL;
    int magic, rc = 0;

    if (!segment) return GLOB_NOSPACE;
    memcpy(segment, rest, seglen);
    segment[seglen] = '\0';
    magic = has_magic(segment, flags);

    if (!magic) {
        char *next = join_path(prefix, segment);
        struct stat st;
        free(segment);
        if (!next) return GLOB_NOSPACE;
        if (!tail || !*tail) {
            if (lstat(next, &st) == 0)
                rc = add_result(g, next, flags);
        } else if (stat(next, &st) == 0 && S_ISDIR(st.st_mode)) {
            rc = walk_pattern(next, tail, flags, errfunc, g);
        }
        free(next);
        return rc;
    } else {
        char *dirpath = NULL;
        DIR *dir;
        struct dirent *de;
        if (!*prefix) dirpath = strdup(".");
        else dirpath = strdup(prefix);
        if (!dirpath) { free(segment); return GLOB_NOSPACE; }
        dir = opendir(dirpath);
        if (!dir) {
            int e = errno;
            if ((flags & GLOB_ERR) || (errfunc && errfunc(dirpath, e)))
                rc = GLOB_ABORTED;
            free(dirpath); free(segment);
            return rc;
        }
        for (;;) {
            char *next;
            errno = 0;
            de = readdir(dir);
            if (de == NULL) {
                if (errno && ((flags & GLOB_ERR)
                    || (errfunc && errfunc(dirpath, errno))))
                    rc = GLOB_ABORTED;
                break;
            }
            struct stat st;
            if (de->d_name[0] == '.' && segment[0] != '.'
                    && !(flags & GLOB_PERIOD))
                continue;
            if (!wildmatch(segment, de->d_name, flags)) continue;
            next = join_path(prefix, de->d_name);
            if (!next) { rc = GLOB_NOSPACE; break; }
            if (!tail || !*tail) {
                if (lstat(next, &st) == 0)
                    rc = add_result(g, next, flags);
            } else if (stat(next, &st) == 0 && S_ISDIR(st.st_mode)) {
                rc = walk_pattern(next, tail, flags, errfunc, g);
            }
            free(next);
            if (rc) break;
        }
        closedir(dir);
        free(dirpath);
        free(segment);
        return rc;
    }
}

void globfree(glob_t *g)
{
    size_t i, extra;
    if (!g) return;
    extra = (g->gl_flags & GLOB_DOOFFS) ? g->gl_offs : 0;
    for (i = 0; i < g->gl_pathc; ++i)
        free(g->gl_pathv[extra + i]);
    free(g->gl_pathv);
    memset(g, 0, sizeof(*g));
}

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int), glob_t *g)
{
    size_t old_count;
    int rc;
    char *prefix = NULL;
    const char *p;

    if (!pattern || !g) return GLOB_ABORTED;
    if (!(flags & GLOB_APPEND)) {
        size_t requested_offs = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;
        memset(g, 0, sizeof(*g));
        g->gl_offs = requested_offs;
    }
    g->gl_flags = flags | (g->gl_flags & GLOB_DOOFFS);
    old_count = g->gl_pathc;

    if (pattern[0] == '/') {
        prefix = strdup("/");
        p = pattern + 1;
    } else {
        prefix = strdup("");
        p = pattern;
    }
    if (!prefix) return GLOB_NOSPACE;
    rc = walk_pattern(prefix, p, flags, errfunc, g);
    free(prefix);

    if (rc) return rc;
    if (g->gl_pathc == old_count) {
        if (flags & GLOB_NOCHECK)
            return add_result(g, pattern, flags);
        return GLOB_NOMATCH;
    }
    if (!(flags & GLOB_NOSORT)) {
        size_t off = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;
        qsort(g->gl_pathv + off, g->gl_pathc, sizeof(char *), compare_strings);
    }
    return 0;
}
