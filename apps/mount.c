#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>

// mount                         list what is mounted
// mount [-t type] device dir    mount a block device
int main(int argc, char **argv) {
    const char *type = NULL;
    int i = 1;

    if (argc == 1) {
        char buf[2048];
        int fd = open("/proc/mounts", O_RDONLY);
        int n;
        if (fd < 0) { printf("mount: cannot read /proc/mounts\n"); return 1; }
        while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, n);
        close(fd);
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "-t") == 0) {
        type = argv[2];
        i = 3;
    }
    if (argc - i != 2) {
        printf("usage: mount [-t ext2] device directory\n");
        return 1;
    }

    if (mount(argv[i], argv[i + 1], type, 0, NULL) < 0) {
        perror("mount");
        return 1;
    }
    return 0;
}