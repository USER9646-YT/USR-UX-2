#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

void keyboard_init(void);
char keyboard_getchar(void);   // blocks (hlt) until a key is available; kernel use only 
int  keyboard_poll(void);      // next byte, or -1 if none is waiting 

// keyboard layouts (see keymap.h): an index into the table of layouts 
void keyboard_set_layout(int index);
int  keyboard_layout(void);

// Next raw scancode straight from the keyboard controller, or -1 if none. Only for use
// while interrupts are still off (the boot menu): with the keyboard interrupt running
// it would steal keys from the driver. Mouse bytes are skipped. 
int  keyboard_poll_scancode(void);

#endif