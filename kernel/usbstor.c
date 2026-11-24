#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "usb.h"
#include "usbstor.h"
#include "kprintf.h"

#define USTOR_MAX    8
#define MAX_SECTORS  (USB_BOUNCE_PAGES * 4096 / 512)        // 128 sectors per command

struct udisk {
    struct usb_dev      dev;
    struct usbstor_disk info;
    uint32_t            tag;
};

static struct udisk udisks[USTOR_MAX];
static int          nudisks;
static uint8_t      cfgbuf[512];

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t get32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

// ---------- Bulk-Only Transport ---------- //

static void clear_halt(struct usb_dev *d, int ep_addr) {
    ehci_control(d, 0x02, 1, 0, (uint16_t)ep_addr, NULL, 0);    // CLEAR_FEATURE(ENDPOINT_HALT)
    if (ep_addr & 0x80) d->in_toggle = 0; else d->out_toggle = 0;
}

static void reset_recovery(struct usb_dev *d) {
    ehci_control(d, 0x21, 0xFF, 0, d->iface, NULL, 0);          // Bulk-Only Mass Storage Reset
    clear_halt(d, d->in_ep | 0x80);
    clear_halt(d, d->out_ep);
}

// Runs one SCSI command. Data travels through usb_bounce. Returns the status of the command
// (0 = good, 1 = failed, 2 = phase error) or -1 if the transport itself failed.
static int bot(struct udisk *u, const uint8_t *cdb, uint8_t cdb_len, int dir_in, uint32_t len) {
    struct usb_dev *d = &u->dev;
    uint8_t *cbw = (uint8_t *)(uintptr_t)usb_cmdpage;
    uint8_t *csw = cbw + 0x100;
    uint32_t cbw_page[1] = { usb_cmdpage };
    uint32_t csw_page[1] = { usb_cmdpage + 0x100 };
    int r;

    memset(cbw, 0, 31);
    put32(cbw, 0x43425355);                                     // "USBC"
    put32(cbw + 4, ++u->tag);
    put32(cbw + 8, len);
    cbw[12] = dir_in ? 0x80 : 0x00;
    cbw[14] = cdb_len;
    memcpy(cbw + 15, cdb, cdb_len);

    if (ehci_bulk(d, d->out_ep, 0, cbw_page, 31) != 0) { reset_recovery(d); return -1; }

    if (len) {
        r = ehci_bulk(d, dir_in ? d->in_ep : d->out_ep, dir_in, usb_bounce, len);
        if (r == -3) clear_halt(d, dir_in ? (d->in_ep | 0x80) : d->out_ep);   // the CSW says why
        else if (r != 0) { reset_recovery(d); return -1; }
    }

    r = ehci_bulk(d, d->in_ep, 1, csw_page, 13);
    if (r == -3) {
        clear_halt(d, d->in_ep | 0x80);
        r = ehci_bulk(d, d->in_ep, 1, csw_page, 13);
    }
    if (r != 0) { reset_recovery(d); return -1; }

    if (get32(csw) != 0x53425355 || get32(csw + 4) != u->tag) {     // "USBS", our tag
        reset_recovery(d);
        return -1;
    }
    return csw[12];
}

// REQUEST SENSE: returns (sense key << 16) | (ASC << 8) | ASCQ, or -1
static int sense(struct udisk *u) {
    static const uint8_t cdb[6] = { 0x03, 0, 0, 0, 18, 0 };
    if (bot(u, cdb, 6, 1, 18) != 0) return -1;
    const uint8_t *s = (const uint8_t *)(uintptr_t)usb_bounce[0];
    return ((s[2] & 0x0F) << 16) | (s[12] << 8) | s[13];
}

static int wait_ready(struct udisk *u) {
    static const uint8_t tur[6] = { 0, 0, 0, 0, 0, 0 };

    for (int tries = 0; tries < 30; tries++) {
        int r = bot(u, tur, 6, 0, 0);
        if (r == 0) return 0;
        if (r == 1) {
            int s = sense(u);
            if (s >= 0 && ((s >> 8) & 0xFF) == 0x3A) return -1;    // no medium in the drive
        }
        usb_delay_ms(100);
    }
    return -1;
}

// ---------- finding the disks ---------- //

int usbstor_attach(const struct usb_dev *dev) {
    if (nudisks >= USTOR_MAX) return -1;
    struct udisk *u = &udisks[nudisks];
    struct usb_dev *d = &u->dev;

    memset(u, 0, sizeof(*u));
    *d = *dev;

    // The configuration descriptor: find a Bulk-Only SCSI interface and its two bulk endpoints
    if (usb_get_descriptor(d, 2, 0, cfgbuf, 9) < 9) return -1;
    uint32_t total = (uint32_t)cfgbuf[2] | ((uint32_t)cfgbuf[3] << 8);
    if (total > sizeof(cfgbuf)) total = sizeof(cfgbuf);
    if (usb_get_descriptor(d, 2, 0, cfgbuf, (uint16_t)total) < 9) return -1;

    int ok = 0;
    for (uint32_t p = 0; p + 2 <= total; ) {
        uint8_t len = cfgbuf[p], type = cfgbuf[p + 1];
        if (len < 2) break;

        if (type == 4 && p + 9 <= total) {                      // an interface
            if (ok && d->in_ep && d->out_ep) break;             // we already have a usable one
            ok = cfgbuf[p + 3] == 0 && cfgbuf[p + 5] == 0x08 &&
                 cfgbuf[p + 6] == 0x06 && cfgbuf[p + 7] == 0x50;
            if (ok) { d->iface = cfgbuf[p + 2]; d->in_ep = d->out_ep = 0; }
        } else if (type == 5 && ok && p + 7 <= total) {         // an endpoint of that interface
            uint8_t addr = cfgbuf[p + 2];
            uint16_t mps = (uint16_t)(cfgbuf[p + 4] | (cfgbuf[p + 5] << 8));
            if ((cfgbuf[p + 3] & 3) == 2) {                     // bulk
                if ((addr & 0x80) && !d->in_ep)       { d->in_ep  = addr & 0x0F; d->in_max  = mps; }
                else if (!(addr & 0x80) && !d->out_ep) { d->out_ep = addr & 0x0F; d->out_max = mps; }
            }
        }
        p += len;
    }
    if (!ok || !d->in_ep || !d->out_ep) return -1;              // not a mass-storage device

    d->config = cfgbuf[5];
    if (ehci_control(d, 0x00, 9, d->config, 0, NULL, 0) < 0) return -1;   // SET_CONFIGURATION
    usb_delay_ms(10);

    // Who are you?
    static const uint8_t inquiry[6] = { 0x12, 0, 0, 0, 36, 0 };
    if (bot(u, inquiry, 6, 1, 36) != 0) return -1;
    const uint8_t *iq = (const uint8_t *)(uintptr_t)usb_bounce[0];
    if ((iq[0] & 0x1F) != 0) return -1;                         // not a plain disk (CD-ROM, tape, ...)

    char *m = u->info.model;
    memcpy(m, iq + 8, 8);                                       // vendor
    m[8] = ' ';
    memcpy(m + 9, iq + 16, 16);                                 // product
    m[25] = '\0';
    for (int k = 24; k >= 0 && (m[k] == ' ' || m[k] == '\0'); k--) m[k] = '\0';

    if (wait_ready(u) != 0) {
        kprintf("usb: %s: no medium or not ready\n", m);
        return -1;
    }

    static const uint8_t readcap[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (bot(u, readcap, 10, 1, 8) != 0) return -1;
    const uint8_t *rc = (const uint8_t *)(uintptr_t)usb_bounce[0];
    uint32_t last = get32be(rc), bsize = get32be(rc + 4);
    if (bsize != 512) {
        kprintf("usb: %s has %u-byte blocks, not supported\n", m, bsize);
        return -1;
    }
    u->info.sectors = last == 0xFFFFFFFFu ? 0xFFFFFFFFu : last + 1;
    u->info.present = 1;

    kprintf("usbstor%d: %s, %u sectors (%u MiB)\n", nudisks, m, u->info.sectors,
            u->info.sectors / 2048);
    nudisks++;
    return 0;
}

int usbstor_init(void) {
    nudisks = 0;
    ehci_init();                        // attaches every storage device it finds
    return nudisks;
}

const struct usbstor_disk *usbstor_get(int idx) {
    if (idx < 0 || idx >= nudisks) return NULL;
    return &udisks[idx].info;
}

// ---------- reading and writing ---------- //

static int xfer(int idx, uint32_t lba, uint32_t count, void *buf, int is_write) {
    if (idx < 0 || idx >= nudisks) return -1;
    struct udisk *u = &udisks[idx];

    if (lba + count < lba) return -1;
    if (lba + count > u->info.sectors) return -1;

    uint8_t *p = buf;
    while (count) {
        uint32_t n = count > MAX_SECTORS ? MAX_SECTORS : count;
        uint32_t bytes = n * 512;
        uint8_t cdb[10];
        int r = -1;

        memset(cdb, 0, sizeof(cdb));
        cdb[0] = is_write ? 0x2A : 0x28;                        // WRITE(10) / READ(10)
        put32be(cdb + 2, lba);
        cdb[7] = (uint8_t)(n >> 8);
        cdb[8] = (uint8_t)n;

        for (int attempt = 0; attempt < 2 && r != 0; attempt++) {
            if (is_write) {                                     // caller -> bounce pages
                const uint8_t *src = p;
                uint32_t left = bytes;
                for (int i = 0; left; i++) {
                    uint32_t c = left < 4096 ? left : 4096;
                    memcpy((void *)(uintptr_t)usb_bounce[i], src, c);
                    src += c;
                    left -= c;
                }
            }
            r = bot(u, cdb, 10, !is_write, bytes);
            if (r > 0) sense(u);                                // clears a pending error condition
        }
        if (r != 0) return -1;

        if (!is_write) {                                        // bounce pages -> caller
            uint8_t *dst = p;
            uint32_t left = bytes;
            for (int i = 0; left; i++) {
                uint32_t c = left < 4096 ? left : 4096;
                memcpy(dst, (const void *)(uintptr_t)usb_bounce[i], c);
                dst += c;
                left -= c;
            }
        }
        lba   += n;
        p     += bytes;
        count -= n;
    }

    if (is_write) {                                             // SYNCHRONIZE CACHE; not every stick has it
        uint8_t sync[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        bot(u, sync, 10, 0, 0);
    }
    return 0;
}

int usbstor_read(int idx, uint32_t lba, uint32_t count, void *buf) {
    return xfer(idx, lba, count, buf, 0);
}

int usbstor_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
    return xfer(idx, lba, count, (void *)buf, 1);
}