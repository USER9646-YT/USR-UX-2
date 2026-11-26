#include <string.h>
#include <ctype.h>
#include <fnmatch.h>

#define WEAK __attribute__((weak))

static int fold(int c, int flags) {
    if ((flags & FNM_CASEFOLD) && c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

// FNM_PERIOD: a '.' at the start (or after a '/' with FNM_PATHNAME) must be matched literally
static int leading_period(const char *s, const char *start, int flags) {
    if (*s != '.') return 0;
    if (s == start) return 1;
    return (flags & FNM_PATHNAME) && s[-1] == '/';
}

static int class_match(const char *name, size_t len, int c) {
    if (len == 5 && !strncmp(name, "alpha", 5))  return isalpha(c) != 0;
    if (len == 5 && !strncmp(name, "digit", 5))  return isdigit(c) != 0;
    if (len == 5 && !strncmp(name, "alnum", 5))  return isalnum(c) != 0;
    if (len == 5 && !strncmp(name, "upper", 5))  return isupper(c) != 0;
    if (len == 5 && !strncmp(name, "lower", 5))  return islower(c) != 0;
    if (len == 5 && !strncmp(name, "space", 5))  return isspace(c) != 0;
    if (len == 5 && !strncmp(name, "punct", 5))  return ispunct(c) != 0;
    if (len == 5 && !strncmp(name, "print", 5))  return isprint(c) != 0;
    if (len == 5 && !strncmp(name, "graph", 5))  return isgraph(c) != 0;
    if (len == 5 && !strncmp(name, "cntrl", 5))  return iscntrl(c) != 0;
    if (len == 6 && !strncmp(name, "xdigit", 6)) return isxdigit(c) != 0;
    if (len == 5 && !strncmp(name, "blank", 5))  return c == ' ' || c == '\t';
    return 0;
}

// Matches c against the bracket expression that starts after the '['.
// 1 = matches, 0 = does not, -1 = no closing ']' (then '[' is an ordinary character)
static int bracket(const char *p, int c, int flags, const char **end) {
    int neg = 0, ok = 0, first = 1;

    c = fold(c, flags);
    if (*p == '!' || *p == '^') { neg = 1; p++; }

    for (;;) {
        int lo, hi;

        if (*p == '\0') return -1;
        if (*p == ']' && !first) break;
        first = 0;

        if (*p == '[' && p[1] == ':') {
            const char *e = strstr(p + 2, ":]");
            if (e) {
                if (class_match(p + 2, (size_t)(e - (p + 2)), c)) ok = 1;
                p = e + 2;
                continue;
            }
        }

        lo = (unsigned char)*p++;
        if (lo == '\\' && !(flags & FNM_NOESCAPE) && *p) lo = (unsigned char)*p++;
        hi = lo;
        if (*p == '-' && p[1] != ']' && p[1] != '\0') {
            p++;
            hi = (unsigned char)*p++;
            if (hi == '\\' && !(flags & FNM_NOESCAPE) && *p) hi = (unsigned char)*p++;
        }
        lo = fold(lo, flags);
        hi = fold(hi, flags);
        if (c >= lo && c <= hi) ok = 1;
    }
    *end = p + 1;
    return ok != neg;
}

static int match(const char *p, const char *s, const char *start, int flags) {
    for (;;) {
        int pc = (unsigned char)*p++;
        int sc = (unsigned char)*s;

        switch (pc) {
        case '\0':
            if (sc == '\0') return 0;
            if ((flags & FNM_LEADING_DIR) && sc == '/') return 0;
            return FNM_NOMATCH;

        case '?':
            if (sc == '\0') return FNM_NOMATCH;
            if ((flags & FNM_PATHNAME) && sc == '/') return FNM_NOMATCH;
            if ((flags & FNM_PERIOD) && leading_period(s, start, flags)) return FNM_NOMATCH;
            s++;
            continue;

        case '*':
            while (*p == '*') p++;
            if ((flags & FNM_PERIOD) && leading_period(s, start, flags)) return FNM_NOMATCH;
            if (*p == '\0') {                               // a trailing star takes the rest
                if (flags & FNM_PATHNAME) {
                    if (strchr(s, '/') == NULL) return 0;
                    return (flags & FNM_LEADING_DIR) ? 0 : FNM_NOMATCH;
                }
                return 0;
            }
            for (;;) {
                if (match(p, s, start, flags) == 0) return 0;
                if (*s == '\0') return FNM_NOMATCH;
                if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
                s++;
            }

        case '[': {
            const char *end;
            int r;

            if (sc == '\0') return FNM_NOMATCH;
            if ((flags & FNM_PATHNAME) && sc == '/') return FNM_NOMATCH;
            if ((flags & FNM_PERIOD) && leading_period(s, start, flags)) return FNM_NOMATCH;
            r = bracket(p, sc, flags, &end);
            if (r < 0) {                                    // no closing bracket: a plain '['
                if (sc != '[') return FNM_NOMATCH;
                s++;
                continue;
            }
            if (!r) return FNM_NOMATCH;
            p = end;
            s++;
            continue;
        }

        case '\\':
            if (!(flags & FNM_NOESCAPE)) {
                pc = (unsigned char)*p;
                if (pc == '\0') pc = '\\'; else p++;
            }
            /* fall through */
        default:
            if (sc == '\0' || fold(pc, flags) != fold(sc, flags)) return FNM_NOMATCH;
            s++;
        }
    }
}

WEAK int fnmatch(const char *pattern, const char *string, int flags) {
    return match(pattern, string, string, flags);
}