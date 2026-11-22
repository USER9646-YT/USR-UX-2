#ifndef _LINUX_VT_H
#define _LINUX_VT_H

#define MIN_NR_CONSOLES 1
#define MAX_NR_CONSOLES 63

// ioctl requests (on a console file descriptor)
#define VT_OPENQRY      0x5600          // find the first available VT (int *)
#define VT_GETMODE      0x5601          // get mode of active VT (struct vt_mode *)
#define VT_SETMODE      0x5602          // set mode of active VT
#define VT_GETSTATE     0x5603          // get global VT state info (struct vt_stat *)
#define VT_SENDSIG      0x5604
#define VT_RELDISP      0x5605          // release display
#define VT_ACTIVATE     0x5606          // make VT n the active one
#define VT_WAITACTIVE   0x5607          // wait until VT n is active
#define VT_DISALLOCATE  0x5608
#define VT_LOCKSWITCH   0x560B
#define VT_UNLOCKSWITCH 0x560C

// vt_mode.mode
#define VT_AUTO    0x00                 // the kernel switches VTs by itself
#define VT_PROCESS 0x01                 // the process is told to release and acquire the display
#define VT_ACKACQ  0x02

struct vt_mode {
    char  mode;
    char  waitv;                        // if set, hang on writes when not active
    short relsig;                       // signal to raise on release request
    short acqsig;                       // signal to raise on acquisition
    short frsig;                        // unused (set to 0)
};

struct vt_stat {
    unsigned short v_active;            // active VT
    unsigned short v_signal;            // signal to send
    unsigned short v_state;             // bitmask of VTs in use
};

#endif