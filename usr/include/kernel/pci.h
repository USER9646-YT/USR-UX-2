#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdint.h>

struct pci_dev {
    uint8_t  bus, dev, fn;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if;
};

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t value);

// Finds the first device with this class code. 0 = found (filled into *out), -1 = none
int      pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                        struct pci_dev *out);
// The n-th such device (n = 0 is the first)
int      pci_find_nth(uint8_t class_code, uint8_t subclass, uint8_t prog_if, int n,
                      struct pci_dev *out);

// Turns on memory-space access and/or bus mastering (DMA) for a device
void     pci_enable(const struct pci_dev *d, int memory, int bus_master);

// Maps a physical register block (uncached) into free kernel address space.
// Call it during boot only: it may add a page table that processes created later share.
volatile uint8_t *pci_map_mmio(uint32_t phys, uint32_t bytes);

#endif