#include <stdint.h>
#include "pci.h"
#include "pmm.h"
#include "paging.h"
#include <stddef.h>

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA    0xCFC

static inline void out32(uint16_t port, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint32_t in32(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
           ((uint32_t)fn << 8) | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    out32(CONFIG_ADDRESS, cfg_addr(bus, dev, fn, off));
    return in32(CONFIG_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t value) {
    out32(CONFIG_ADDRESS, cfg_addr(bus, dev, fn, off));
    out32(CONFIG_DATA, value);
}

int pci_find_nth(uint8_t class_code, uint8_t subclass, uint8_t prog_if, int n, struct pci_dev *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x00);
            if ((id & 0xFFFF) == 0xFFFF) continue;              // nothing in this slot

            uint32_t hdr = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            int functions = ((hdr >> 16) & 0x80) ? 8 : 1;       // multi-function device?

            for (int fn = 0; fn < functions; fn++) {
                id = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;

                uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08);
                if ((uint8_t)(cls >> 24) != class_code) continue;
                if ((uint8_t)(cls >> 16) != subclass)   continue;
                if ((uint8_t)(cls >> 8)  != prog_if)    continue;
                if (n-- > 0) continue;

                out->bus        = (uint8_t)bus;
                out->dev        = (uint8_t)dev;
                out->fn         = (uint8_t)fn;
                out->vendor     = (uint16_t)(id & 0xFFFF);
                out->device     = (uint16_t)(id >> 16);
                out->class_code = class_code;
                out->subclass   = subclass;
                out->prog_if    = prog_if;
                return 0;
            }
        }
    }
    return -1;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, struct pci_dev *out) {
    return pci_find_nth(class_code, subclass, prog_if, 0, out);
}

void pci_enable(const struct pci_dev *d, int memory, int bus_master) {
    uint32_t v = pci_read32(d->bus, d->dev, d->fn, 0x04) & 0xFFFFu;   // command register only:
    if (memory)     v |= 1u << 1;                                     // the upper half is the status
    if (bus_master) v |= 1u << 2;                                     // register (write-1-to-clear)
    pci_write32(d->bus, d->dev, d->fn, 0x04, v);
}

volatile uint8_t *pci_map_mmio(uint32_t phys, uint32_t bytes) {
    uint32_t base  = phys & ~0xFFFu;
    uint32_t pages = ((phys & 0xFFFu) + bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t v = 0xF8000000u; v + pages * PAGE_SIZE <= 0xFC000000u; v += PAGE_SIZE) {
        int free_run = 1;
        for (uint32_t i = 0; i < pages && free_run; i++)
            if (paging_get_phys(v + i * PAGE_SIZE)) free_run = 0;
        if (!free_run) continue;

        for (uint32_t i = 0; i < pages; i++)
            if (paging_map(v + i * PAGE_SIZE, base + i * PAGE_SIZE,
                           PTE_WRITE | 0x18) != 0)            // 0x18 = write-through + cache disabled
                return NULL;
        return (volatile uint8_t *)(uintptr_t)(v + (phys & 0xFFFu));
    }
    return NULL;
}