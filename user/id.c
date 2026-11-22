#include <unistd.h>
#include "ulib.h"
#include "pwd.h"

int main(void) {
    struct passwd pw;
    unsigned int uid = (unsigned int)getuid();

    puts("uid=");
    print_int((int)uid);
    if (pw_lookup(0, uid, &pw) == 0) { puts("("); puts(pw.name); puts(")"); }
    puts(" gid=");
    print_int(getgid());
    puts("\n");
    return 0;
}