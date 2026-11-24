#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>

// Finds the mount point of a device name ("disk1p1") in /proc/mounts
static int find_mountpoint(const char *dev, char *dir, int size) {
    static char buf[2048];
    int fd = open("/proc/mounts", O_RDONLY);
    int n;

    if (fd < 0) return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    char *p = buf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char save = *eol;
        *eol = '\0';

        char *sp1 = strchr(p, ' ');
        if (sp1) {
            *sp1 = '\0';
            char *mp = sp1 + 1;
            char *sp2 = strchr(mp, ' ');
            if (sp2) *sp2 = '\0';
            if (strcmp(p, dev) == 0 && (int)strlen(mp) < size) {
                strcpy(dir, mp);
                return 0;
            }
        }
        if (!save) break;
        p = eol + 1;
    }
    return -1;
}

int main(int argc, char **argv) {
    char dir[128];
    const char *target;

    if (argc != 2) {
        printf("usage: umount directory | /dev/diskXpY\n");
        return 1;
    }
    target = argv[1];

    if (strncmp(target, "/dev/", 5) == 0) {
        if (find_mountpoint(target + 5, dir, sizeof(dir)) != 0) {
            printf("umount: %s is not mounted\n", target);
            return 1;
        }
        target = dir;
    }

    if (umount(target) < 0) {
        perror("umount");
        return 1;
    }
    return 0;
}