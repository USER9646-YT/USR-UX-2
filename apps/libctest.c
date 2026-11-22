#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(int argc, char **argv) {
    printf("hello from the libc (argc=%d, argv[0]=%s)\n", argc, argv[0]);

    printf("ints:    [%d] [%5d] [%-5d] [%05d] [%+d] [%x] [%X] [%o] [%u]\n",
           -42, 42, 42, 42, 42, 255, 255, 8, 4000000000u);
    printf("floats:  [%f] [%.3f] [%10.2f] [%e] [%g] [%g]\n",
           3.14159265358979, 2.0 / 3.0, 1234.5678, 12345.6789, 0.0001234, 1e10);
    printf("long:    [%lld] [%llu] [%llx]\n", -9000000000000LL, 18000000000000ULL, 0xDEADBEEFCAFEULL);
    printf("strings: [%s] [%10s] [%-10s] [%.3s] [%c] [%%]\n", "hello", "right", "left", "truncated", 'Z');
    printf("math:    sqrt(2)=%.15f sin(1)=%.15f exp(1)=%.15f\n", sqrt(2.0), sin(1.0), exp(1.0));

    int v[8] = { 5, 3, 9, 1, 7, 2, 8, 4 };
    qsort(v, 8, sizeof(v[0]), cmp_int);
    printf("qsort:   ");
    for (int i = 0; i < 8; i++) printf("%d ", v[i]);
    printf("\n");

    char *blocks[200];                              // malloc / free / realloc over brk
    for (int i = 0; i < 200; i++) {
        blocks[i] = malloc(1000 + i);
        memset(blocks[i], i, 1000 + i);
    }
    for (int i = 0; i < 200; i += 2) free(blocks[i]);
    char *grown = realloc(blocks[1], 50000);
    printf("malloc:  grown block keeps its data: %s\n", grown && grown[0] == 1 && grown[999] == 1 ? "yes" : "NO");
    for (int i = 1; i < 200; i += 2) free(i == 1 ? grown : blocks[i]);

    int n; double d; char w[32];
    int got = sscanf("42 3.5 word", "%d %lf %31s", &n, &d, w);
    printf("sscanf:  %d fields: %d %.1f %s\n", got, n, d, w);

    time_t t = time(NULL);
    char when[64];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
    printf("time:    %s\n", when);

    printf("env:     HOME=%s USER=%s\n", getenv("HOME") ? getenv("HOME") : "(unset)",
           getenv("USER") ? getenv("USER") : "(unset)");
    printf("errno:   %s\n", strerror(ENOENT));

    FILE *f = fopen("/tmp/libctest.txt", "w");      // files and buffering
    if (f) {
        fprintf(f, "line one\nline two %d\n", 2);
        fclose(f);
        f = fopen("/tmp/libctest.txt", "r");
        char line[64];
        while (f && fgets(line, sizeof(line), f)) printf("file:    %s", line);
        if (f) fclose(f);
    } else {
        perror("fopen");
    }
    return 0;
}