#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"
#include "pwd.h"

static void mode_string(unsigned int m, char *s) {
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' : '-';
    s[1] = (m & 0400) ? 'r' : '-';
    s[2] = (m & 0200) ? 'w' : '-';
    s[3] = (m & 0100) ? 'x' : '-';
    s[4] = (m & 0040) ? 'r' : '-';
    s[5] = (m & 0020) ? 'w' : '-';
    s[6] = (m & 0010) ? 'x' : '-';
    s[7] = (m & 0004) ? 'r' : '-';
    s[8] = (m & 0002) ? 'w' : '-';
    s[9] = (m & 0001) ? 'x' : '-';
    s[10] = '\0';
}

static void print_owner(unsigned int uid) {
    struct passwd pw;
    if (pw_lookup(0, uid, &pw) == 0) puts(pw.name);
    else                             print_int((int)uid);
}

int main(int argc, char **argv) {
    int longfmt = 0;
    const char *dir = ".";

    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-l")) longfmt = 1;
        else dir = argv[i];
    }

    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        puts("ls: cannot open ");
        puts(dir);
        puts("\n");
        return 1;
    }

    struct dirent de;
    while (readdir(fd, &de) > 0) {
        if (longfmt) {
            char full[600];
            unsigned int n = 0;
            for (const char *p = dir; *p && n < 300; p++) full[n++] = *p;
            full[n++] = '/';
            for (const char *p = de.d_name; *p && n < 590; p++) full[n++] = *p;
            full[n] = '\0';

            struct stat st;
            if (stat(full, &st) == 0) {
                char ms[11];
                mode_string(st.st_mode, ms);
                puts(ms);                  puts(" ");
                print_int((int)st.st_uid); puts(" ");
                print_int((int)st.st_gid); puts(" ");
                print_int((int)st.st_size); puts(" ");
            }
        }
        puts(de.d_name);
        puts("\n");
    }

    close(fd);
    return 0;
}