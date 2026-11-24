#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "usb.h"
#include "pci.h"
#include "pmm.h"
#include "io.h"
#include "kprintf.h"

// ---------- registers ---------- //

#define OP_USBCMD     0x00
#define OP_USBSTS     0x04
#define OP_USBINTR    0x08
#define OP_CTRLDSSEG  0x10
#define OP_ASYNCLIST  0x18
#define OP_CONFIGFLAG 0x40
#define OP_PORTSC     0x44

#define CMD_RS        (1u << 0)
#define CMD_HCRESET   (1u << 1)
#define CMD_ASE       (1u << 5)
#define CMD_IAAD      (1u << 6)

#define STS_IAA       (1u << 5)
#define STS_HALTED    (1u << 12)

#define PORT_CCS      (1u << 0)
#define PORT_CSC      (1u << 1)
#define PORT_PED      (1u << 2)
#define PORT_PEDC     (1u << 3)
#define PORT_OCC      (1u << 5)
#define PORT_RESET    (1u << 8)
#define PORT_PP       (1u << 12)
#define PORT_OWNER    (1u << 13)
#define PORT_WC       (PORT_CSC | PORT_PEDC | PORT_OCC)   // write-1-to-clear bits: keep them 0

// ---------- data structures (the controller reads these from RAM) ---------- //

#define PTR_T   1u              // terminate
#define PTR_QH  (1u << 1)       // pointer type: queue head

#define QTD_ACTIVE   (1u << 7)
#define QTD_HALTED   (1u << 6)
#define QTD_DBERR    (1u << 5)
#define QTD_BABBLE   (1u << 4)
#define QTD_XACTERR  (1u << 3)
#define QTD_PID_OUT  (0u << 8)
#define QTD_PID_IN   (1u << 8)
#define QTD_PID_SETUP (2u << 8)
#define QTD_CERR3    (3u << 10)
#define QTD_IOC      (1u << 15)
#define QTD_TOGGLE   (1u << 31)

struct qtd {                    // 64 bytes: the 64-bit-capable layout, the high halves stay 0
    uint32_t next, alt, token;
    uint32_t buf[5];
    uint32_t buf_hi[5];
    uint32_t pad[3];
};

struct qh {                     // 128 bytes
    uint32_t link, ep_char, ep_cap, cur;
    uint32_t next, alt, token;              // the "overlay": a copy of the running qTD
    uint32_t buf[5];
    uint32_t buf_hi[5];
    uint32_t pad[15];
};

// Layout of one controller's DMA page
#define QH_HEAD   0x000         // permanent head of the async list (never executed)
#define QH_WORK   0x080         // the queue head we run a transfer on
#define QTD_POOL  0x100         // 16 qTDs
#define QTD_COUNT 16

#define MAX_HC    4
#define RUN_LIMIT 250000u       // x 20 us = 5 s per transfer

#define OPR(e, off) (*(volatile uint32_t *)((e)->op + (off)))
#define PHYS(p)     ((uint32_t)(uintptr_t)(p))

struct ehci {
    volatile uint8_t *op;       // operational registers
    int      nports;
    uint32_t mem;               // DMA page: head QH, work QH, qTD pool
    int      next_addr;         // next USB address to hand out
};

static struct ehci hcs[MAX_HC];
static int         nhc;

uint32_t usb_bounce[USB_BOUNCE_PAGES];
uint32_t usb_cmdpage;

// ---------- time ---------- //

// Port 0x80 reads take about a microsecond on real hardware (and are slower elsewhere),
// which is good enough for the delays USB needs.
static void delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us; i++) inb(0x80);
}

static void delay_ms(uint32_t ms) {
    while (ms--) delay_us(1000);
}

void usb_delay_ms(uint32_t ms) {
    delay_ms(ms);
}

// Waits until (reg & mask) == value. 0 = ok, -1 = timeout
static int wait_bits(struct ehci *e, uint32_t off, uint32_t mask, uint32_t value, uint32_t ms) {
    for (uint32_t i = 0; i < ms * 10; i++) {
        if ((OPR(e, off) & mask) == value) return 0;
        delay_us(100);
    }
    return -1;
}

// ---------- running transfers ---------- //

static void qtd_fill(struct qtd *q, uint32_t pid, int toggle, uint32_t buf, uint32_t len, int ioc) {
    memset(q, 0, sizeof(*q));
    q->next  = PTR_T;
    q->alt   = PTR_T;
    q->token = QTD_ACTIVE | pid | QTD_CERR3 | (len << 16) |
               (toggle ? QTD_TOGGLE : 0) | (ioc ? QTD_IOC : 0);
    q->buf[0] = buf;                            // may carry an offset inside its page
}

static struct qtd *qtd_at(struct ehci *e, int i) {
    return (struct qtd *)(uintptr_t)(e->mem + QTD_POOL + (uint32_t)i * sizeof(struct qtd));
}

// Chains n qTDs from the pool, runs them on endpoint ep of device d. 0, -1, -2 or -3
static int run(struct ehci *e, const struct usb_dev *d, uint32_t ep, uint32_t maxpkt, int n) {
    struct qh *head = (struct qh *)(uintptr_t)(e->mem + QH_HEAD);
    struct qh *work = (struct qh *)(uintptr_t)(e->mem + QH_WORK);
    volatile struct qtd *q = (volatile struct qtd *)qtd_at(e, 0);
    int result = 0;
    uint32_t i;

    for (int k = 0; k < n - 1; k++) qtd_at(e, k)->next = PHYS(qtd_at(e, k + 1));

    memset(work, 0, sizeof(*work));
    work->link    = (e->mem + QH_HEAD) | PTR_QH;                // a ring of two queue heads
    work->ep_char = d->addr | (ep << 8) | (2u << 12)            // high speed
                  | (1u << 14)                                   // toggle comes from the qTDs
                  | (maxpkt << 16) | (4u << 28);
    work->ep_cap  = 1u << 30;
    work->next    = PHYS(qtd_at(e, 0));
    work->alt     = PTR_T;
    work->token   = 0;

    __asm__ volatile ("" ::: "memory");
    head->link = (e->mem + QH_WORK) | PTR_QH;                   // the controller can see it now

    for (i = 0; i < RUN_LIMIT; i++) {
        int pending = 0;
        for (int k = 0; k < n; k++) {
            uint32_t t = q[k].token;
            if (t & (QTD_HALTED | QTD_DBERR | QTD_BABBLE | QTD_XACTERR)) {
                result = (t & (QTD_DBERR | QTD_BABBLE | QTD_XACTERR)) ? -1 : -3;   // -3: stall
                break;
            }
            if (t & QTD_ACTIVE) { pending = 1; break; }
        }
        if (result || !pending) break;
        delay_us(20);
    }
    if (!result && i == RUN_LIMIT) result = -2;

    // Take our queue head out of the list and wait until the controller has let go of it
    head->link = (e->mem + QH_HEAD) | PTR_QH;
    OPR(e, OP_USBCMD) |= CMD_IAAD;
    wait_bits(e, OP_USBSTS, STS_IAA, STS_IAA, 100);
    OPR(e, OP_USBSTS) = STS_IAA;
    return result;
}

int ehci_control(struct usb_dev *d, uint8_t reqtype, uint8_t req, uint16_t value,
                 uint16_t index, void *data, uint16_t len) {
    struct ehci *e = d->hc;
    uint8_t *cp = (uint8_t *)(uintptr_t)usb_cmdpage;
    int in = (reqtype & 0x80) != 0;
    int n = 0;

    if (len > 0xE00) return -1;

    cp[0] = reqtype;  cp[1] = req;
    cp[2] = (uint8_t)value; cp[3] = (uint8_t)(value >> 8);
    cp[4] = (uint8_t)index; cp[5] = (uint8_t)(index >> 8);
    cp[6] = (uint8_t)len;   cp[7] = (uint8_t)(len >> 8);
    if (len && !in) memcpy(cp + 0x100, data, len);

    qtd_fill(qtd_at(e, n++), QTD_PID_SETUP, 0, usb_cmdpage, 8, 0);
    if (len)
        qtd_fill(qtd_at(e, n++), in ? QTD_PID_IN : QTD_PID_OUT, 1, usb_cmdpage + 0x100, len, 0);
    qtd_fill(qtd_at(e, n++), in ? QTD_PID_OUT : QTD_PID_IN, 1, 0, 0, 1);    // status stage

    int r = run(e, d, 0, d->maxpkt0, n);
    if (r < 0) return r;

    int got = 0;
    if (len) {
        got = len - (int)((qtd_at(e, 1)->token >> 16) & 0x7FFF);
        if (got < 0) got = 0;
        if (in) memcpy(data, cp + 0x100, (uint32_t)got);
    }
    return got;
}

// pages[0] may carry an offset (for the CBW/CSW), the others must be whole pages
int ehci_bulk(struct usb_dev *d, int ep, int in, const uint32_t *pages, uint32_t len) {
    struct ehci *e = d->hc;
    uint32_t maxpkt = in ? d->in_max : d->out_max;
    int toggle = in ? d->in_toggle : d->out_toggle;
    uint32_t done = 0, pg = 0;
    int n = 0;

    if (len == 0 || maxpkt == 0) return -1;

    while (done < len) {
        if (n >= QTD_COUNT) return -1;
        uint32_t chunk = len - done;
        if (chunk > 4 * PAGE_SIZE) chunk = 4 * PAGE_SIZE;       // 4 pages per qTD

        struct qtd *q = qtd_at(e, n);
        qtd_fill(q, in ? QTD_PID_IN : QTD_PID_OUT, toggle, pages[pg], chunk, done + chunk == len);

        uint32_t npg = (chunk + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint32_t k = 1; k < npg; k++) q->buf[k] = pages[pg + k];
        pg += npg;

        toggle ^= (int)(((chunk + maxpkt - 1) / maxpkt) & 1);   // an odd number of packets flips it
        done += chunk;
        n++;
    }

    int r = run(e, d, (uint32_t)ep, maxpkt, n);
    if (r == 0) {
        if (in) d->in_toggle = (uint8_t)toggle; else d->out_toggle = (uint8_t)toggle;
    }
    return r;
}

int usb_get_descriptor(struct usb_dev *d, uint8_t type, uint8_t index, void *buf, uint16_t len) {
    return ehci_control(d, 0x80, 6, (uint16_t)((type << 8) | index), 0, buf, len);
}

// ---------- bringing up a controller ---------- //

static int hc_init(struct ehci *e, const struct pci_dev *pd) {
    uint32_t bar = pci_read32(pd->bus, pd->dev, pd->fn, 0x10);
    if (bar & 1) return -1;                                     // an I/O BAR: not EHCI

    pci_enable(pd, 1, 1);

    volatile uint8_t *cap = pci_map_mmio(bar & ~0xFu, 0x1000);
    if (!cap) { kprintf("usb: cannot map the EHCI registers\n"); return -1; }

    uint32_t hcs = *(volatile uint32_t *)(cap + 4);
    uint32_t hcc = *(volatile uint32_t *)(cap + 8);
    e->op     = cap + cap[0];
    e->nports = (int)(hcs & 0xF);

    // Take the controller over from the BIOS (it may emulate a USB keyboard through SMIs)
    uint32_t eecp = (hcc >> 8) & 0xFF;
    for (int guard = 0; eecp >= 0x40 && guard < 16; guard++) {
        uint32_t v = pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)eecp);
        if ((v & 0xFF) == 1) {                                  // the legacy-support capability
            if (v & (1u << 16)) {                               // the BIOS owns it
                pci_write32(pd->bus, pd->dev, pd->fn, (uint8_t)eecp, v | (1u << 24));
                for (int i = 0; i < 1000; i++) {
                    delay_ms(1);
                    v = pci_read32(pd->bus, pd->dev, pd->fn, (uint8_t)eecp);
                    if (!(v & (1u << 16))) break;
                }
            }
            pci_write32(pd->bus, pd->dev, pd->fn, (uint8_t)(eecp + 4), 0);   // no more SMIs
            break;
        }
        eecp = (v >> 8) & 0xFF;
    }

    // Stop it, then reset it
    OPR(e, OP_USBCMD) &= ~CMD_RS;
    wait_bits(e, OP_USBSTS, STS_HALTED, STS_HALTED, 50);
    OPR(e, OP_USBCMD) = CMD_HCRESET;
    if (wait_bits(e, OP_USBCMD, CMD_HCRESET, 0, 500)) {
        kprintf("usb: EHCI reset timed out\n");
        return -1;
    }

    e->mem = pmm_alloc();
    if (!e->mem) return -1;
    memset((void *)(uintptr_t)e->mem, 0, PAGE_SIZE);

    // The head of the async list: a queue head that points at itself and never runs
    struct qh *head = (struct qh *)(uintptr_t)(e->mem + QH_HEAD);
    head->link    = (e->mem + QH_HEAD) | PTR_QH;
    head->ep_char = 1u << 15;                                   // "head of reclamation list"
    head->ep_cap  = 1u << 30;
    head->next    = PTR_T;
    head->alt     = PTR_T;
    head->token   = QTD_HALTED;

    OPR(e, OP_CTRLDSSEG)  = 0;
    OPR(e, OP_USBINTR)    = 0;                                  // we poll
    OPR(e, OP_USBSTS)     = 0x3F;
    OPR(e, OP_ASYNCLIST)  = e->mem + QH_HEAD;
    OPR(e, OP_USBCMD)     = (8u << 16) | CMD_ASE | CMD_RS;
    if (wait_bits(e, OP_USBSTS, STS_HALTED, 0, 100)) {
        kprintf("usb: EHCI does not start\n");
        return -1;
    }
    OPR(e, OP_CONFIGFLAG) = 1;                                  // route all ports to us
    delay_ms(20);

    if (hcs & (1u << 4)) {                                      // ports have power switches
        for (int p = 0; p < e->nports; p++)
            OPR(e, OP_PORTSC + 4 * p) |= PORT_PP;
    }
    delay_ms(120);                                              // power-up and debounce

    e->next_addr = 1;
    return 0;
}

// Looks at every port of a controller and brings up what is connected
static void enumerate(struct ehci *e, int hc_idx) {
    for (int p = 0; p < e->nports; p++) {
        uint32_t off = OP_PORTSC + 4u * (uint32_t)p;
        uint32_t sc = OPR(e, off);

        if (!(sc & PORT_CCS)) continue;
        kprintf("usb: ehci%d port %d: something is connected\n", hc_idx, p);

        if (((sc >> 10) & 3) == 1) {                            // K state: a low-speed device
            OPR(e, off) = (sc & ~PORT_WC) | PORT_OWNER;         // belongs to the companion controller
            kprintf("usb: ehci%d port %d: low-speed device, not supported\n", hc_idx, p);
            continue;
        }

        delay_ms(100);                                          // debounce

        sc = OPR(e, off);                                       // reset the port
        OPR(e, off) = (sc & ~(PORT_WC | PORT_PED)) | PORT_RESET;
        delay_ms(60);
        sc = OPR(e, off);
        OPR(e, off) = sc & ~(PORT_WC | PORT_RESET);
        for (int i = 0; i < 20 && (OPR(e, off) & PORT_RESET); i++) delay_ms(1);
        delay_ms(10);

        sc = OPR(e, off);
        if (!(sc & PORT_PED)) {                                 // not high speed: hand it over
            OPR(e, off) = (sc & ~PORT_WC) | PORT_OWNER;
            kprintf("usb: ehci%d port %d: full-speed device, not supported\n", hc_idx, p);
            continue;
        }

        struct usb_dev dev;
        uint8_t desc[18];

        memset(&dev, 0, sizeof(dev));
        dev.hc      = e;
        dev.port    = (uint8_t)p;
        dev.maxpkt0 = 64;

        if (usb_get_descriptor(&dev, 1, 0, desc, 8) < 8) {
            kprintf("usb: ehci%d port %d: the device does not answer\n", hc_idx, p);
            continue;
        }
        dev.maxpkt0 = desc[7] ? desc[7] : 64;

        uint8_t addr = (uint8_t)e->next_addr++;
        if (ehci_control(&dev, 0x00, 5, addr, 0, NULL, 0) < 0) {            // SET_ADDRESS
            kprintf("usb: ehci%d port %d: cannot set an address\n", hc_idx, p);
            continue;
        }
        delay_ms(10);
        dev.addr = addr;

        if (usb_get_descriptor(&dev, 1, 0, desc, 18) < 18) continue;
        dev.vid = (uint16_t)(desc[8] | (desc[9] << 8));
        dev.pid = (uint16_t)(desc[10] | (desc[11] << 8));
        kprintf("usb: ehci%d port %d: high-speed device %04x:%04x (class %02x)\n",
                hc_idx, p, dev.vid, dev.pid, desc[4]);

        usbstor_attach(&dev);
    }
}

int ehci_init(void) {
    struct pci_dev pd;

    nhc = 0;
    if (pci_find_nth(0x0C, 0x03, 0x20, 0, &pd) != 0) return 0;     // no EHCI controller

    usb_cmdpage = pmm_alloc();
    if (!usb_cmdpage) return 0;
    memset((void *)(uintptr_t)usb_cmdpage, 0, PAGE_SIZE);
    for (int i = 0; i < USB_BOUNCE_PAGES; i++) {
        usb_bounce[i] = pmm_alloc();
        if (!usb_bounce[i]) { kprintf("usb: out of memory\n"); return 0; }
        memset((void *)(uintptr_t)usb_bounce[i], 0, PAGE_SIZE);
    }

    for (int n = 0; n < MAX_HC && pci_find_nth(0x0C, 0x03, 0x20, n, &pd) == 0; n++) {
        struct ehci *e = &hcs[nhc];
        memset(e, 0, sizeof(*e));

        kprintf("usb: EHCI controller %04x:%04x at %02x:%02x.%x\n",
                pd.vendor, pd.device, pd.bus, pd.dev, pd.fn);
        if (hc_init(e, &pd) != 0) continue;
        enumerate(e, nhc);
        nhc++;
    }
    return nhc;
}