#include <unistd.h>
#include <stdio.h>
#include <string.h>

char *optarg;
int optind = 1, opterr = 1, optopt;

static int nextchar;                        // position inside a group such as -abc

int getopt(int argc, char *const argv[], const char *optstring) {
    int colon = (optstring[0] == ':');
    const char *a;

    optarg = NULL;

    if (nextchar == 0) {
        if (optind >= argc) return -1;
        a = argv[optind];
        if (!a || a[0] != '-' || a[1] == '\0') return -1;           // not an option, or a lone "-"
        if (a[1] == '-' && a[2] == '\0') { optind++; return -1; }   // "--" ends the options
        nextchar = 1;
    }

    a = argv[optind];
    int c = (unsigned char)a[nextchar++];
    const char *spec = (c == ':') ? NULL : strchr(optstring + colon, c);

    if (!spec) {
        optopt = c;
        if (a[nextchar] == '\0') { optind++; nextchar = 0; }
        if (opterr && !colon) fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
        return '?';
    }

    if (spec[1] == ':') {                                           // this option takes an argument
        if (a[nextchar] != '\0') {
            optarg = (char *)&a[nextchar];                          // -ofile
        } else if (optind + 1 < argc) {
            optarg = argv[++optind];                                // -o file
        } else {
            optopt = c;
            optind++;
            nextchar = 0;
            if (opterr && !colon)
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
            return colon ? ':' : '?';
        }
        optind++;
        nextchar = 0;
    } else if (a[nextchar] == '\0') {
        optind++;
        nextchar = 0;
    }
    return c;
}