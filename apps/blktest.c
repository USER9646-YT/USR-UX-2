#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
    unsigned char b[2];

    int fd = open("/dev/disk0", O_RDONLY);
    if (fd < 0) { printf("cannot open /dev/disk0\n"); return 1; }
    lseek(fd, 510, SEEK_SET);
    read(fd, b, 2);
    printf("MBR signature at 510: %x %x (expect 55 aa)\n", b[0], b[1]);
    printf("disk size: %d bytes\n", (int)lseek(fd, 0, SEEK_END));
    close(fd);

    fd = open("/dev/disk0p1", O_RDONLY);
    if (fd < 0) { printf("cannot open /dev/disk0p1\n"); return 1; }
    lseek(fd, 1080, SEEK_SET);                  // ext2 superblock magic: 0xEF53
    read(fd, b, 2);
    printf("ext2 magic at 1080: %x %x (expect 53 ef)\n", b[0], b[1]);
    close(fd);
    return 0;
}