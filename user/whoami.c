#include <unistd.h>
#include "ulib.h"
#include "pwd.h"

int main(void) {
    struct passwd pw;
    unsigned int uid = (unsigned int)getuid();

    if (pw_lookup(0, uid, &pw) == 0) puts(pw.name);
    else                             print_int((int)uid);
    puts("\n");
    return 0;
}