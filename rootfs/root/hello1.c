#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    long long big = 9000000000000LL;
    unsigned int u = 4000000000u;
    double pi = 3.14159265358979;
    char name[32];

    printf("Hello, world! (from a program built by tcc)\n");
    printf("argc=%d argv[0]=%s\n", argc, argv[0]);

    printf("int:     [%d] [%5d] [%-5d] [%05d] [%+d]\n", -42, 42, 42, 42, 42);
    printf("unsigned/hex: [%u] [%x] [%X] [%o]\n", u, 255, 255, 8);
    printf("long long:    [%lld] [%llx]\n", big, big);
    printf("string:  [%s] [%10s] [%-10s] [%.3s]\n", "tcc", "right", "left", "truncated");
    printf("char:    [%c] [%c]\n", 'O', 'K');
    printf("double:  [%f] [%.3f] [%10.2f] [%e] [%g]\n", pi, pi, pi * 100, pi, pi);

    strcpy(name, "USR/UX");
    printf("sum of 1..10 = %d, 22/7 = %.4f, name has %d letters\n",
           55, 22.0 / 7.0, (int)strlen(name));

    printf("big / 7 = %lld, big %% 7 = %lld\n", big / 7, big % 7);   // uses the 64-bit divide helpers
    return 0;
}