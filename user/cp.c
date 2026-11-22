#include <unistd.h>
#include "ulib.h"
#include "fsutil.h"

int main(int argc, char **argv) {
    int recursive = 0, i = 1;

    if (i < argc && (streq(argv[i], "-r") || streq(argv[i], "-R"))) {
        recursive = 1;
        i++;
    }
    if (argc - i != 2) {
        puts("usage: cp [-r] SOURCE DEST\n");
        return 1;
    }

    const char *src = argv[i];
    const char *dst = argv[i + 1];
    char target[PATH_BUF];

    if (fs_is_dir(src) && !recursive) {
        puts("cp: -r not specified; omitting directory ");
        puts(src);
        puts("\n");
        return 1;
    }

    if (fs_is_dir(dst)) {                         // cp file dir/  ->  dir/file
        if (fs_join(target, sizeof(target), dst, fs_basename(src)) < 0) {
            eprint("cp", dst, -36);
            return 1;
        }
        dst = target;
    }

    return fs_copy_tree("cp", src, dst, 0) < 0 ? 1 : 0;
}