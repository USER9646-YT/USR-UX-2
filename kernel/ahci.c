#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ahci.h"
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include "kprintf.h"

// ---------- HBA (controller) registers ---------- //

#define HBA_CAP   0x00
#define HBA_GHC   0x04
#define HBA_IS    0x08
#define HBA_PI    0x0C
#define HBA_VS    0x10
#define HBA_CAP2  0x24
#define HBA_BOHC  0x28

#define GHC_IE    (1u << 1)
#define GHC_AE    (1u << 31)

// ---------- per-port registers (offsets inside the port's 0x80 bytes) ---------- //

#define PORT_BASE(p) (0x100u + (p) * 0x80u)

#define P_CLB   0x00
#define P_CLBU  0x04
#define P_FB    0x08
#define P_FBU   0x0C
#define P_IS    0x10
#define P_IE    0x14
#define P_CMD   0x18
#define P_TFD   0x20
#define P_SIG   0x24
#define P_SSTS  0x28
#define P_SERR  0x30
#define P_CI    0x38

#define CMD_ST   (1u << 0)
#define CMD_FRE  (1u << 4)
#define CMD_FR   (1u << 14)
#define CMD_CR   (1u << 15)

#define TFD_ERR  0x01u
#define TFD_DRQ  0x08u
#define TFD_DF   0x20u
#define TFD_BSY  0x80u

#define IS_TFES  (1u << 30)                // task file error

#define SIG_ATA  0x00000101u               // a plain SATA drive

// ---------- ATA commands ---------- //

#define ATA_READ_DMA_EXT   0x25
#define ATA_WRITE_DMA_EXT  0x35
#define ATA_FLUSH_EXT      0xEA
#define ATA_IDENTIFY       0xEC

// ---------- our memory layout ---------- //

#define POLL_LIMIT    2000000u
#define BOUNCE_PAGES  16                                   // 64 KiB per command
#define MAX_SECTORS   (BOUNCE_PAGES * PAGE_SIZE / 512)     // 128

// One page per port: command list (1 KiB aligned), FIS area (256 aligned), command table (128 aligned)
#define CMDLIST_OFF   0x000
#define FIS_OFF       0x400
#define CMDTBL_OFF    0x500

struct cmd_header {                 // one of 32 in the command list; we only use slot 0
    uint32_t flags;                 // bits 0-4: FIS length in dwords, bit 6: write, bits 16-31: PRDT entries
    uint32_t prdbc;                 // bytes transferred (written by the controller)
    uint32_t ctba;                  // physical address of the command table
    uint32_t ctbau;
    uint32_t reserved[4];
};

struct prdt_entry {                 // one chunk of the data buffer
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc;                   // byte count - 1
};

struct port {
    int               num;          // port number on the controller
    uint32_t          mem;          // physical (= virtual) page with list, FIS area and table
    struct ahci_drive info;
};

static volatile uint8_t *abar;                  // the controller's registers (mapped)
static struct port       ports[AHCI_MAX_DRIVES];
static int               nports;
static uint32_t          bounce[BOUNCE_PAGES];  // physical pages the data goes through

static uint32_t rd(uint32_t off)             { return *(volatile uint32_t *)(abar + off); }
static void     wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(abar + off) = v; }

// Waits until (register & mask) == 0. 0 = ok, -1 = timeout
static int wait_clear(uint32_t off, uint32_t mask) {
    for (uint32_t i = 0; i < POLL_LIMIT; i++)
        if (!(rd(off) & mask)) return 0;
    return -1;
}

// ---------- starting and stopping a port ---------- //

static int port_stop(uint32_t b) {
    wr(b + P_CMD, rd(b + P_CMD) & ~CMD_ST);
    if (wait_clear(b + P_CMD, CMD_CR)) return -1;
    wr(b + P_CMD, rd(b + P_CMD) & ~CMD_FRE);
    return wait_clear(b + P_CMD, CMD_FR);
}

static int port_start(uint32_t b) {
    if (wait_clear(b + P_CMD, CMD_CR)) return -1;
    wr(b + P_CMD, rd(b + P_CMD) | CMD_FRE);
    wr(b + P_CMD, rd(b + P_CMD) | CMD_FRE | CMD_ST);
    return 0;
}

// After a failed command: stop and restart the port to clear the error state
static void port_recover(struct port *pt) {
    uint32_t b = PORT_BASE((uint32_t)pt->num);
    port_stop(b);
    wr(b + P_SERR, 0xFFFFFFFFu);
    wr(b + P_IS, 0xFFFFFFFFu);
    port_start(b);
}

// ---------- running one command ---------- //

// Register host-to-device FIS: command, LBA (32 bits here) and sector count
static void fis_h2d(uint8_t *f, uint8_t cmd, uint32_t lba, uint16_t count) {
    memset(f, 0, 20);
    f[0]  = 0x27;                   // register H2D
    f[1]  = 0x80;                   // this is a command
    f[2]  = cmd;
    f[4]  = (uint8_t)lba;
    f[5]  = (uint8_t)(lba >> 8);
    f[6]  = (uint8_t)(lba >> 16);
    f[7]  = 0x40;                   // LBA mode
    f[8]  = (uint8_t)(lba >> 24);
    f[12] = (uint8_t)count;
    f[13] = (uint8_t)(count >> 8);
}

// Sends the FIS on slot 0; `bytes` of data travel through the bounce pages. 0 = ok
static int port_cmd(struct port *pt, const uint8_t *fis, int is_write, uint32_t bytes) {
    uint32_t b = PORT_BASE((uint32_t)pt->num);
    struct cmd_header *h = (struct cmd_header *)(uintptr_t)(pt->mem + CMDLIST_OFF);
    uint8_t *tbl = (uint8_t *)(uintptr_t)(pt->mem + CMDTBL_OFF);
    struct prdt_entry *prdt = (struct prdt_entry *)(tbl + 0x80);
    uint32_t n = 0;

    if (wait_clear(b + P_TFD, TFD_BSY | TFD_DRQ)) {         // the drive is not ready
        port_recover(pt);
        return -2;
    }

    memset(tbl, 0, 0x80 + BOUNCE_PAGES * sizeof(struct prdt_entry));
    memcpy(tbl, fis, 20);

    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE, n++) {
        uint32_t chunk = bytes - off < PAGE_SIZE ? bytes - off : PAGE_SIZE;
        prdt[n].dba  = bounce[n];
        prdt[n].dbau = 0;
        prdt[n].dbc  = chunk - 1;
    }

    h->flags = 5u | (is_write ? (1u << 6) : 0u) | (n << 16);
    h->prdbc = 0;
    h->ctba  = (uint32_t)(uintptr_t)tbl;
    h->ctbau = 0;

    __asm__ volatile ("" ::: "memory");                     // the tables must be written before CI
    wr(b + P_IS, 0xFFFFFFFFu);
    wr(b + P_CI, 1);

    uint32_t i;
    for (i = 0; i < POLL_LIMIT; i++) {
        if (rd(b + P_IS) & IS_TFES) break;                  // the drive reported an error
        if (!(rd(b + P_CI) & 1)) break;                     // finished
    }

    if (i == POLL_LIMIT) {
        port_recover(pt);
        return -2;
    }
    if ((rd(b + P_IS) & IS_TFES) || (rd(b + P_TFD) & (TFD_ERR | TFD_DF))) {
        port_recover(pt);
        return -1;
    }
    wr(b + P_IS, 0xFFFFFFFFu);
    return 0;
}

static void bounce_put(const uint8_t *src, uint32_t bytes) {     // caller -> bounce pages
    for (int i = 0; bytes; i++) {
        uint32_t n = bytes < PAGE_SIZE ? bytes : PAGE_SIZE;
        memcpy((void *)(uintptr_t)bounce[i], src, n);
        src += n;
        bytes -= n;
    }
}

static void bounce_get(uint8_t *dst, uint32_t bytes) {           // bounce pages -> caller
    for (int i = 0; bytes; i++) {
        uint32_t n = bytes < PAGE_SIZE ? bytes : PAGE_SIZE;
        memcpy(dst, (const void *)(uintptr_t)bounce[i], n);
        dst += n;
        bytes -= n;
    }
}

// ---------- finding the drives ---------- //

static int identify(struct port *pt) {
    uint8_t fis[20];

    fis_h2d(fis, ATA_IDENTIFY, 0, 0);
    fis[7] = 0;
    if (port_cmd(pt, fis, 0, 512) != 0) return -1;

    const uint16_t *id = (const uint16_t *)(uintptr_t)bounce[0];
    struct ahci_drive *d = &pt->info;

    if (!((id[83] >> 10) & 1)) {                                 // no 48-bit LBA: not supported
        kprintf("ahci: port %d has no LBA48, skipped\n", pt->num);
        return -1;
    }
    if ((id[106] & 0xC000) == 0x4000 && (id[106] & 0x1000)) {    // sectors longer than 512 bytes?
        uint32_t words = (uint32_t)id[117] | ((uint32_t)id[118] << 16);
        if (words != 256) {
            kprintf("ahci: port %d has %u-byte sectors, skipped\n", pt->num, words * 2);
            return -1;
        }
    }

    if (id[102] || id[103]) d->sectors = 0xFFFFFFFFu;            // more than 2 TiB: clamp
    else d->sectors = (uint32_t)id[100] | ((uint32_t)id[101] << 16);
    if (d->sectors == 0) return -1;

    for (int k = 0; k < 20; k++) {                               // model: words 27-46, high byte first
        d->model[k * 2]     = (char)(id[27 + k] >> 8);
        d->model[k * 2 + 1] = (char)(id[27 + k] & 0xFF);
    }
    d->model[40] = '\0';
    for (int k = 39; k >= 0 && d->model[k] == ' '; k--) d->model[k] = '\0';

    d->present = 1;
    return 0;
}

// Sets up the memory and starts the port. 0 = ok
static int port_setup(struct port *pt, int num) {
    uint32_t b = PORT_BASE((uint32_t)num);

    if (port_stop(b) != 0) return -1;

    uint32_t mem = pmm_alloc();
    if (!mem) return -1;
    memset((void *)(uintptr_t)mem, 0, PAGE_SIZE);

    pt->num = num;
    pt->mem = mem;

    wr(b + P_CLB,  mem + CMDLIST_OFF);
    wr(b + P_CLBU, 0);
    wr(b + P_FB,   mem + FIS_OFF);
    wr(b + P_FBU,  0);
    wr(b + P_SERR, 0xFFFFFFFFu);
    wr(b + P_IS,   0xFFFFFFFFu);
    wr(b + P_IE,   0);                                           // we poll

    return port_start(b);
}

// A free 8 KiB stretch of kernel address space for the registers
static uint32_t find_window(void) {
    for (uint32_t v = 0xF8000000u; v < 0xFC000000u; v += 2 * PAGE_SIZE)
        if (!paging_get_phys(v) && !paging_get_phys(v + PAGE_SIZE)) return v;
    return 0;
}

int ahci_init(void) {
    struct pci_dev dev;

    nports = 0;
    if (pci_find_class(0x01, 0x06, 0x01, &dev) != 0) return 0;   // no AHCI controller

    uint32_t bar = pci_read32(dev.bus, dev.dev, dev.fn, 0x24);   // BAR5 = the registers
    if ((bar & 1) || (bar & ~0xFu) == 0) {
        kprintf("ahci: unusable BAR5 (%08x)\n", bar);
        return 0;
    }
    uint32_t phys = bar & ~0xFu;

    pci_enable(&dev, 1, 1);                                      // memory space + DMA

    uint32_t win = find_window();
    if (!win) { kprintf("ahci: no free address space for the registers\n"); return 0; }

    uint32_t base = phys & ~0xFFFu;
    for (uint32_t i = 0; i < 2; i++)                             // 0x1100 bytes of registers
        if (paging_map(win + i * PAGE_SIZE, base + i * PAGE_SIZE,
                       PTE_WRITE | 0x18) != 0) {                 // 0x18 = write-through + cache disabled
            kprintf("ahci: cannot map the registers\n");
            return 0;
        }
    abar = (volatile uint8_t *)(uintptr_t)(win + (phys & 0xFFFu));

    kprintf("ahci: controller %04x:%04x at %02x:%02x.%x, version %x.%x\n",
            dev.vendor, dev.device, dev.bus, dev.dev, dev.fn,
            rd(HBA_VS) >> 16, rd(HBA_VS) & 0xFFFFu);

    // Take the controller over from the firmware if it still owns it
    if (rd(HBA_CAP2) & 1) {
        uint32_t bohc = rd(HBA_BOHC);
        if (bohc & 1) {
            wr(HBA_BOHC, bohc | 2);
            wait_clear(HBA_BOHC, 1);
        }
    }
    wr(HBA_GHC, (rd(HBA_GHC) | GHC_AE) & ~GHC_IE);               // AHCI mode, interrupts off
    wr(HBA_IS, 0xFFFFFFFFu);

    for (int i = 0; i < BOUNCE_PAGES; i++) {                     // the shared data pages
        bounce[i] = pmm_alloc();
        if (!bounce[i]) { kprintf("ahci: out of memory\n"); return 0; }
        memset((void *)(uintptr_t)bounce[i], 0, PAGE_SIZE);
    }

    uint32_t implemented = rd(HBA_PI);
    for (int p = 0; p < 32 && nports < AHCI_MAX_DRIVES; p++) {
        if (!(implemented & (1u << p))) continue;

        uint32_t b = PORT_BASE((uint32_t)p);
        uint32_t ssts = rd(b + P_SSTS);
        if ((ssts & 0xF) != 3 || ((ssts >> 8) & 0xF) != 1) continue;   // no device / link not active
        if (wait_clear(b + P_TFD, TFD_BSY | TFD_DRQ)) continue;        // never got ready
        if (rd(b + P_SIG) != SIG_ATA) continue;                        // ATAPI, port multiplier, ...

        struct port *pt = &ports[nports];
        memset(pt, 0, sizeof(*pt));
        if (port_setup(pt, p) != 0 || identify(pt) != 0) continue;

        kprintf("ahci%d: %s, %u sectors (%u MiB)\n", nports, pt->info.model,
                pt->info.sectors, pt->info.sectors / 2048);
        nports++;
    }
    return nports;
}

const struct ahci_drive *ahci_get(int idx) {
    if (idx < 0 || idx >= nports) return NULL;
    return &ports[idx].info;
}

// ---------- reading and writing ---------- //

static int ahci_xfer(int idx, uint32_t lba, uint32_t count, void *buf, int is_write) {
    if (idx < 0 || idx >= nports) return -1;
    struct port *pt = &ports[idx];

    if (lba + count < lba) return -1;                            // overflow
    if (lba + count > pt->info.sectors) return -1;               // past the end

    uint8_t *p = buf;
    uint8_t fis[20];

    while (count) {
        uint32_t n = count > MAX_SECTORS ? MAX_SECTORS : count;
        uint32_t bytes = n * 512;

        if (is_write) bounce_put(p, bytes);
        fis_h2d(fis, is_write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT, lba, (uint16_t)n);
        int r = port_cmd(pt, fis, is_write, bytes);
        if (r) return r;
        if (!is_write) bounce_get(p, bytes);

        lba   += n;
        p     += bytes;
        count -= n;
    }

    if (is_write) {                                              // flush the drive's write cache
        fis_h2d(fis, ATA_FLUSH_EXT, 0, 0);
        fis[7] = 0;
        return port_cmd(pt, fis, 0, 0);
    }
    return 0;
}

int ahci_read(int idx, uint32_t lba, uint32_t count, void *buf) {
    return ahci_xfer(idx, lba, count, buf, 0);
}

int ahci_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
    return ahci_xfer(idx, lba, count, (void *)buf, 1);
}