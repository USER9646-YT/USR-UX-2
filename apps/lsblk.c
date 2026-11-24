#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/blkinfo.h>

struct mnt { char dev[24]; char dir[64]; };
static struct mnt mnts[16];
static int nmnts;

// Reads /proc/mounts ("disk0p1 / ext2 rw 0 0") and keeps the block-device entries
static void load_mounts(void) {
    static char buf[2048];
    int fd = open("/proc/mounts", O_RDONLY);
    int n;

    if (fd < 0) return;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *p = buf;
    while (*p && nmnts < 16) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char save = *eol;
        *eol = '\0';

        char *sp1 = strchr(p, ' ');
        if (sp1) {
            *sp1 = '\0';
            char *dir = sp1 + 1;
            char *sp2 = strchr(dir, ' ');
            if (sp2) *sp2 = '\0';
            if (strncmp(p, "disk", 4) == 0 && strlen(p) < sizeof(mnts[0].dev) &&
                strlen(dir) < sizeof(mnts[0].dir)) {
                strcpy(mnts[nmnts].dev, p);
                strcpy(mnts[nmnts].dir, dir);
                nmnts++;
            }
        }
        if (!save) break;
        p = eol + 1;
    }
}

static const char *mountpoint(const char *name) {
    for (int i = 0; i < nmnts; i++)
        if (strcmp(mnts[i].dev, name) == 0) return mnts[i].dir;
    return "";
}

static void pad(const char *s, int width) {
    int n = (int)strlen(s);
    printf("%s", s);
    while (n++ < width) printf(" ");
}

static void size_str(uint32_t sectors, char *out) {
    uint32_t mib = sectors / 2048;
    if (mib >= 1024) {
        uint32_t tenths = mib * 10 / 1024;
        sprintf(out, "%u.%uG", tenths / 10, tenths % 10);
    } else if (mib > 0) {
        sprintf(out, "%uM", mib);
    } else {
        sprintf(out, "%uK", sectors / 2);
    }
}

static int show(const char *path, const char *name) {
    struct blkinfo bi;
    char size[16];

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    if (ioctl(fd, BLKGETINFO, &bi) < 0) { close(fd); return 0; }
    close(fd);

    size_str(bi.sectors, size);
    pad(name, 12);
    pad(size, 8);
    pad(bi.part ? "part" : "disk", 6);
    if (bi.part) {
        pad("", 6);
        pad("", 3);
        pad("", 22);
    } else {
        pad(bi.bus, 6);
        pad(bi.flags & 1 ? "1" : "0", 3);
        pad(bi.model, 22);
    }
    printf("%s\n", mountpoint(name));
    return 1;
}

int main(void) {
    char path[32], name[24];
    int found = 0;

    load_mounts();

    pad("NAME", 12); pad("SIZE", 8); pad("TYPE", 6); pad("TRAN", 6); pad("RM", 3);
    pad("MODEL", 22);
    printf("MOUNTPOINT\n");

    for (int d = 0; d < 16; d++) {
        sprintf(name, "disk%d", d);
        sprintf(path, "/dev/%s", name);
        if (!show(path, name)) continue;
        found++;

        for (int p = 1; p < 32; p++) {
            sprintf(name, "disk%dp%d", d, p);
            sprintf(path, "/dev/%s", name);
            show(path, name);
        }
    }
    if (!found) printf("(no block devices)\n");
    return 0;
}