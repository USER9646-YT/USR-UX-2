#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ata.h"
#include "ahci.h"
#include "io.h"
#include "kprintf.h"
#include "ahci.h"
#include "usbstor.h"

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
#define IDE_DRIVES 4                // 2 buses x master/slave

static const uint16_t bus_base[2] = { 0x1F0, 0x170 };
static const uint16_t bus_ctrl[2] = { 0x3F6, 0x376 };

static struct ata_drive ide[IDE_DRIVES];     // the legacy IDE slots

// The disks the rest of the kernel sees
enum { DISK_IDE, DISK_AHCI, DISK_USB };
struct disk {
    int              kind;
    int              sub;           // IDE slot or AHCI drive number
    struct ata_drive info;
};
static struct disk disks[ATA_MAX_DRIVES];
static int         ndisks;

// ---------- legacy IDE (PIO) ---------- //

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

static int ide_identify(int idx) {
    int bus = idx >> 1, slave = idx & 1;
    uint16_t base = bus_base[bus];
    struct ata_drive *d = &ide[idx];
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

    if (inb(base + REG_STATUS) == 0) return 0;         // no device

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

// Set up the registers for a transfer of n (1..255) sectors starting at lba
static void ide_setup(int bus, int slave, uint32_t lba, uint32_t n, uint8_t cmd) {
    uint16_t base = bus_base[bus];

    outb(base + REG_DRIVE, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    ata_delay(bus);
    outb(base + REG_COUNT, (uint8_t)n);
    outb(base + REG_LBA0, (uint8_t)(lba));
    outb(base + REG_LBA1, (uint8_t)(lba >> 8));
    outb(base + REG_LBA2, (uint8_t)(lba >> 16));
    outb(base + REG_CMD, cmd);
}

static int ide_read(int idx, uint32_t lba, uint32_t count, void *buf) {
    int bus = idx >> 1, slave = idx & 1;
    uint16_t base = bus_base[bus];
    uint16_t *p = buf;

    while (count) {
        uint32_t n = count > 255 ? 255 : count;     // a count byte of 0 would mean 256
        ide_setup(bus, slave, lba, n, CMD_READ);

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

static int ide_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
    int bus = idx >> 1, slave = idx & 1;
    uint16_t base = bus_base[bus];
    const uint16_t *p = buf;

    while (count) {
        uint32_t n = count > 255 ? 255 : count;
        ide_setup(bus, slave, lba, n, CMD_WRITE);

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

// ---------- the disk list ---------- //

static void add_disk(int kind, int sub, const struct ata_drive *info) {
    if (ndisks >= ATA_MAX_DRIVES) return;
    disks[ndisks].kind = kind;
    disks[ndisks].sub  = sub;
    disks[ndisks].info = *info;
    ndisks++;
}

int ata_init(void) {
    ndisks = 0;

    // nIEN: stop the drives from raising IRQ 14/15; we poll instead
    outb(bus_ctrl[0], 0x02);
    outb(bus_ctrl[1], 0x02);

    for (int i = 0; i < IDE_DRIVES; i++) {
        if (ide_identify(i)) {
            kprintf("ata%d: %s, %u sectors (%u MiB)\n", i, ide[i].model,
                    ide[i].sectors, ide[i].sectors / 2048);
            add_disk(DISK_IDE, i, &ide[i]);
        }
    }

    int sata = ahci_init();                         // SATA drives behind an AHCI controller
    for (int j = 0; j < sata; j++) {
        const struct ahci_drive *a = ahci_get(j);
        if (!a) continue;

        struct ata_drive info;
        memset(&info, 0, sizeof(info));
        info.present = 1;
        info.sectors = a->sectors;
        memcpy(info.model, a->model, sizeof(info.model));
        add_disk(DISK_AHCI, j, &info);
    }

    int usb = usbstor_init();                       // USB mass-storage devices (EHCI)
    for (int k = 0; k < usb; k++) {
        const struct usbstor_disk *u = usbstor_get(k);
        if (!u) continue;

        struct ata_drive info;
        memset(&info, 0, sizeof(info));
        info.present = 1;
        info.sectors = u->sectors;
        memcpy(info.model, u->model, sizeof(info.model));
        add_disk(DISK_USB, k, &info);
    }
    return ndisks;
}

const struct ata_drive *ata_get(int idx) {
    if (idx < 0 || idx >= ndisks) return NULL;
    return &disks[idx].info;
}

const char *ata_bus_name(int idx) {
    if (idx < 0 || idx >= ndisks) return "";
    switch (disks[idx].kind) {
    case DISK_IDE:  return "ide";
    case DISK_AHCI: return "sata";
    default:        return "usb";
    }
}

int ata_is_removable(int idx) {
    return idx >= 0 && idx < ndisks && disks[idx].kind == DISK_USB;
}

static int ata_check(int idx, uint32_t lba, uint32_t count) {
    if (idx < 0 || idx >= ndisks) return -1;
    if (lba + count < lba) return -1;                       // overflow
    if (lba + count > disks[idx].info.sectors) return -1;   // past the end
    return 0;
}

int ata_read(int idx, uint32_t lba, uint32_t count, void *buf) {
    if (ata_check(idx, lba, count) != 0) return -1;
    struct disk *d = &disks[idx];
    switch (d->kind) {
    case DISK_IDE:  return ide_read(d->sub, lba, count, buf);
    case DISK_AHCI: return ahci_read(d->sub, lba, count, buf);
    default:        return usbstor_read(d->sub, lba, count, buf);
    }
}

int ata_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
    if (ata_check(idx, lba, count) != 0) return -1;
    struct disk *d = &disks[idx];
    switch (d->kind) {
    case DISK_IDE:  return ide_write(d->sub, lba, count, buf);
    case DISK_AHCI: return ahci_write(d->sub, lba, count, buf);
    default:        return usbstor_write(d->sub, lba, count, buf);
    }
}