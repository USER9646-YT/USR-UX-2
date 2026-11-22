#include <unistd.h>
#include "ulib.h"

static void put_num(int v, int width) {
    char t[12];
    int i = 11;
    unsigned int u = v < 0 ? -(unsigned int)v : (unsigned int)v;

    t[i] = '\0';
    do { t[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) t[--i] = '-';

    for (int k = 11 - i; k < width; k++) puts(" ");
    puts(t + i);
}

int main(void) {
    struct procinfo pi;

    puts("  PID  PPID   UID S NAME\n");
    for (int i = 0; ; i++) {
        int r = procinfo(i, &pi);
        if (r < 0) break;
        if (r == 0) continue;

        put_num(pi.pid, 5);  puts(" ");
        put_num(pi.ppid, 5); puts(" ");
        put_num(pi.uid, 5);  puts(" ");

        char s[2] = { pi.state, '\0' };
        puts(s);
        puts(" ");
        puts(pi.name);
        puts("\n");
    }
    return 0;
}