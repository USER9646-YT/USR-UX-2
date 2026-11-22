#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ata.h"
#include "io.h"
#include "kprintf.h"

// Status register bits
#define SR_ERR 0x01
#define SR_DRQ 0x08
#define SR_DF  0x20
#define SR_BSY 0x80

// Commands
#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

// Register offsets from the bus base port
#define REG_DATA   0
#define REG_COUNT  2
#define REG_LBA0   3
#define REG_LBA1   4
#define REG_LBA2   5
#define REG_DRIVE  6
#define REG_STATUS 7
#define REG_CMD    7

#define POLL_LIMIT 1000000u

static const uint16_t bus_base[2] = { 0x1F0, 0x170 };
static const uint16_t bus_ctrl[2] = { 0x3F6, 0x376 };

static struct ata_drive drives[ATA_MAX_DRIVES];

// ~400ns: reading the alternate status register 4 times
static void ata_delay(int bus) {
    for (int i = 0; i < 4; i++) inb(bus_ctrl[bus]);
}

// Wait for BSY to clear (and DRQ to set if need_drq). 0 = ok, <0 = error
static int ata_poll(int bus, int need_drq) {
    uint16_t base = bus_base[bus];
    ata_delay(bus);

    for (uint32_t i = 0; i < POLL_LIMIT; i++) {
        uint8_t s = inb(base + REG_STATUS);
        if (s & SR_BSY) continue;
        if (s & (SR_ERR | SR_DF)) return -1;
        if (!need_drq || (s & SR_DRQ)) return 0;
    }
    return -2;      // timeout
}

static int ata_identify(int idx) {
    int bus = idx >> 1, slave = idx & 1;
    uint16_t base = bus_base[bus];
    struct ata_drive *d = &drives[idx];
    uint32_t i;

    memset(d, 0, sizeof(*d));

    if (inb(base + REG_STATUS) == 0xFF) return 0;      // floating bus: nothing here

    outb(base + REG_DRIVE, 0xA0 | (slave << 4));
    ata_delay(bus);

    outb(base + REG_COUNT, 0);
    outb(base + REG_LBA0, 0);
    outb(base + REG_LBA1, 0);
    outb(base + REG_LBA2, 0);
    outb(base + REG_CMD, CMD_IDENTIFY);
    ata_delay(bus);

    if (inb(base + REG_STATUS) == 0) return 0;         /* no device */

    for (i = 0; i < POLL_LIMIT; i++)
        if (!(inb(base + REG_STATUS) & SR_BSY)) break;
    if (i == POLL_LIMIT) return 0;

    // ATAPI/SATA devices put a signature here instead of answering IDENTIFY
    if (inb(base + REG_LBA1) || inb(base + REG_LBA2)) return 0;

    for (i = 0; i < POLL_LIMIT; i++) {
        uint8_t s = inb(base + REG_STATUS);
        if (s & SR_ERR) return 0;
        if (s & SR_DRQ) break;
    }
    if (i == POLL_LIMIT) return 0;

    uint16_t id[256];
    for (int w = 0; w < 256; w++) id[w] = inw(base + REG_DATA);

    d->sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);

    // Model string: words 27-46, two chars per word, high byte first
    for (int k = 0; k < 20; k++) {
        d->model[k * 2]     = (char)(id[27 + k] >> 8);
        d->model[k * 2 + 1] = (char)(id[27 + k] & 0xFF);
    }
    d->model[40] = '\0';
    for (int k = 39; k >= 0 && d->model[k] == ' '; k--) d->model[k] = '\0';

    d->present = 1;
    return 1;
}

int ata_init(void) {
    int found = 0;

    // nIEN: stop the drives from raising IRQ 14/15; we poll instead
    outb(bus_ctrl[0], 0x02);
    outb(bus_ctrl[1], 0x02);

    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        if (ata_identify(i)) {
            found++;
            kprintf("ata%d: %s, %u sectors (%u MiB)\n", i, drives[i].model,
                    drives[i].sectors, drives[i].sectors / 2048);
        }
    }
    return found;
}

const struct ata_drive *ata_get(int idx) {
    if (idx < 0 || idx >= ATA_MAX_DRIVES || !drives[idx].present) return NULL;
    return &drives[idx];
}

// Set up the registers for a transfer of n (1..255) sectors starting at lba
static void ata_setup(int bus, int slave, uint32_t lba, uint32_t n, uint8_t cmd) {
    uint16_t base = bus_base[bus];

    outb(base + REG_DRIVE, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    ata_delay(bus);
    outb(base + REG_COUNT, (uint8_t)n);
    outb(base + REG_LBA0, (uint8_t)(lba));
    outb(base + REG_LBA1, (uint8_t)(lba >> 8));
    outb(base + REG_LBA2, (uint8_t)(lba >> 16));
    outb(base + REG_CMD, cmd);
}

static int ata_check(int idx, uint32_t lba, uint32_t count) {
    if (idx < 0 || idx >= ATA_MAX_DRIVES || !drives[idx].present) return -1;
    if (lba + count < lba) return -1;                       // overflow
    if (lba + count > drives[idx].sectors) return -1;       // past the end
    return 0;
}

int ata_read(int idx, uint32_t lba, uint32_t count, void *buf) {
    if (ata_check(idx, lba, count) != 0) return -1;

    int bus = idx >> 1, slave = idx & 1;
    uint16_t base = bus_base[bus];
    uint16_t *p = buf;

    while (count) {
        uint32_t n = count > 255 ? 255 : count;     // a count byte of 0 would mean 256
        ata_setup(bus, slave, lba, n, CMD_READ);

        for (uint32_t s = 0; s < n; s++) {
            int r = ata_poll(bus, 1);
            if (r) return r;
            for (int w = 0; w < 256; w++) *p++ = inw(base + REG_DATA);
        }
        lba += n;
        count -= n;
    }
    return 0;
}

int ata_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
    if (ata_check(idx, lba, count) != 0) return -1;

    int bus = idx >> 1, slave = idx & 1;
    uint16_t base = bus_base[bus];
    const uint16_t *p = buf;

    while (count) {
        uint32_t n = count > 255 ? 255 : count;
        ata_setup(bus, slave, lba, n, CMD_WRITE);

        for (uint32_t s = 0; s < n; s++) {
            int r = ata_poll(bus, 1);
            if (r) return r;
            for (int w = 0; w < 256; w++) outw(base + REG_DATA, *p++);
        }
        lba += n;
        count -= n;
    }

    outb(base + REG_CMD, CMD_FLUSH);                // flush the drive's write cache
    return ata_poll(bus, 0);
}