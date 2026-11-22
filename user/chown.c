#include <unistd.h>
#include "ulib.h"
#include "pwd.h"

/* usage: chown USER[:GID] FILE   (USER may be a name or a number) */
int main(int argc, char **argv) {
    if (argc != 3) {
        puts("usage: chown USER[:GID] FILE\n");
        return 1;
    }

    char owner[32];
    unsigned int i = 0;
    const char *p = argv[1];
    while (*p && *p != ':' && i < sizeof(owner) - 1) owner[i++] = *p++;
    owner[i] = '\0';

    int gid = -1;
    if (*p == ':') gid = (int)parse_uint(p + 1);

    int uid = -1;
    if (owner[0] >= '0' && owner[0] <= '9') {
        uid = (int)parse_uint(owner);
    } else if (owner[0]) {
        struct passwd pw;
        if (pw_lookup(owner, 0, &pw) < 0) {
            puts("chown: unknown user\n");
            return 1;
        }
        uid = (int)pw.uid;
    }

    int r = chown(argv[2], uid, gid);
    if (r < 0) {
        eprint("chown", argv[2], r);
        return 1;
    }
    return 0;
}