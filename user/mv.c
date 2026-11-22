#include <unistd.h>
#include <errno.h>
#include "ulib.h"
#include "fsutil.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        puts("usage: mv SOURCE DEST\n");
        return 1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];
    char target[PATH_BUF];

    if (fs_is_dir(dst)) {                         // mv file dir/  ->  dir/file
        if (fs_join(target, sizeof(target), dst, fs_basename(src)) < 0) {
            eprint("mv", dst, -36);
            return 1;
        }
        dst = target;
    }

    int r = rename(src, dst);
    if (r == -EXDEV) {                            // different filesystems: copy, then delete
        if (fs_copy_tree("mv", src, dst, 0) < 0) return 1;
        return fs_remove_tree("mv", src, 1, 0) < 0 ? 1 : 0;
    }
    if (r < 0) {
        eprint("mv", src, r);
        return 1;
    }
    return 0;
}