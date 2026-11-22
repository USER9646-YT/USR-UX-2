#include <stdint.h>
#include "part.h"
#include "ata.h"

struct mbr_part {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba;
    uint32_t sectors;
} __attribute__((packed));

int part_find_linux(int drive, uint32_t *start_lba, uint32_t *sectors) {
    uint8_t sec[ATA_SECTOR_SIZE];

    if (ata_read(drive, 0, 1, sec) != 0) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -1;      // no partition table

    const struct mbr_part *p = (const struct mbr_part *)(sec + 446);
    for (int i = 0; i < 4; i++) {
        if (p[i].type == 0x83 && p[i].sectors > 0) {
            *start_lba = p[i].lba;
            *sectors   = p[i].sectors;
            return 0;
        }
    }
    return -1;
}