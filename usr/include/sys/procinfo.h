#ifndef _SYS_PROCINFO_H
#define _SYS_PROCINFO_H

struct procinfo {
    int  pid;
    int  ppid;
    int  uid;
    char state; // R running/runnable, S sleeping, Z zombie
    char name[16];
};

#endif