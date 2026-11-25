#include <stdio.h>

int main(int argc, char *argv[]) {
    while (1) {
        if (argc <= 1) {
            if (puts("y") == EOF) {
                break;
            }
        } else {
            for (int i = 1; i < argc; i++) {
                if (i > 1 && putchar(' ') == EOF) {
                    return 0;
                }
                if (fputs(argv[i], stdout) == EOF) {
                    return 0;
                }
            }
            if (putchar('\n') == EOF) {
                break;
            }
        }
    }
    return 0;
}
