#ifndef KERNEL_USB_H
#define KERNEL_USB_H

#include <stdint.h>

#define USB_BOUNCE_PAGES 16            // data pages for bulk transfers: 64 KiB

struct ehci;

struct usb_dev {
    struct ehci *hc;
    uint8_t  addr;
    uint8_t  port;
    uint16_t maxpkt0;
    uint16_t vid, pid;
    // bulk-only mass storage
    uint8_t  config, iface;
    uint8_t  in_ep, out_ep;             // endpoint numbers (without the direction bit)
    uint16_t in_max, out_max;
    uint8_t  in_toggle, out_toggle;     // data toggle of the next packet
};

extern uint32_t usb_bounce[USB_BOUNCE_PAGES];   // physical (= virtual) data pages
extern uint32_t usb_cmdpage;                    // physical page for CBW/CSW and control data

int  ehci_init(void);                           // finds controllers and their devices
// Control transfer on endpoint 0. Returns the bytes of the data stage, or < 0
int  ehci_control(struct usb_dev *d, uint8_t reqtype, uint8_t req, uint16_t value,
                  uint16_t index, void *data, uint16_t len);
// Bulk transfer of len bytes through the given pages. 0 = ok, -1 error, -2 timeout, -3 stall
int  ehci_bulk(struct usb_dev *d, int ep, int in, const uint32_t *pages, uint32_t len);
int  usb_get_descriptor(struct usb_dev *d, uint8_t type, uint8_t index, void *buf, uint16_t len);
void usb_delay_ms(uint32_t ms);

// Called for every device that answered: 0 = it is a mass-storage device and became a disk
int  usbstor_attach(const struct usb_dev *d);

#endif