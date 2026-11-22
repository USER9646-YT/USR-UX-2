#ifndef LIBC_INTERNAL_H
#define LIBC_INTERNAL_H

#include <stddef.h>
#include <errno.h>

// the kernel's system call numbers 
#define SYS_exit       1
#define SYS_fork       2
#define SYS_read       3
#define SYS_write      4
#define SYS_open       5
#define SYS_close      6
#define SYS_waitpid    7
#define SYS_unlink    10
#define SYS_execve    11
#define SYS_chdir     12
#define SYS_time      13
#define SYS_chmod     15
#define SYS_chown     16
#define SYS_lseek     19
#define SYS_getpid    20
#define SYS_setuid    23
#define SYS_getuid    24
#define SYS_access    33
#define SYS_sync      36
#define SYS_kill      37
#define SYS_rename    38
#define SYS_mkdir     39
#define SYS_rmdir     40
#define SYS_dup       41
#define SYS_pipe      42
#define SYS_brk       45
#define SYS_setgid    46
#define SYS_getgid    47
#define SYS_geteuid   49
#define SYS_getegid   50
#define SYS_ioctl     54
#define SYS_fcntl     55
#define SYS_setpgid   57
#define SYS_umask     60
#define SYS_dup2      63
#define SYS_getppid   64
#define SYS_getpgrp   65
#define SYS_sethostname 74
#define SYS_gettimeofday 78
#define SYS_reboot    88
#define SYS_readdir   89
#define SYS_munmap    91
#define SYS_stat     106
#define SYS_lstat    107
#define SYS_fstat    108
#define SYS_uname    122
#define SYS_getpgid  132
#define SYS_select   142
#define SYS_nanosleep 162
#define SYS_poll     168
#define SYS_getcwd   183
#define SYS_mmap2    192
#define SYS_clock_gettime 265

long __syscall6(long n, long a, long b, long c, long d, long e, long f);   // asm.S 

static inline long __syscall3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

// kernel result (-errno on failure) -> C convention (-1 and errno) 
static inline long __sys_ret(long r) {
    if ((unsigned long)r > (unsigned long)-4096L) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

// ---- stdio ---- //
#define F_READ     0x001
#define F_WRITE    0x002
#define F_EOF      0x004
#define F_ERR      0x008
#define F_NOBUF    0x010
#define F_LINEBUF  0x020
#define F_OWNBUF   0x040
#define F_MALLOCED 0x080

struct _FILE {
    int            fd;
    int            flags;
    unsigned char *buf;
    size_t         bufsize;
    size_t         rpos, rend;         // read buffer: bytes [rpos, rend) are unread 
    size_t         wpos;               // write buffer: bytes [0, wpos) are pending 
    int            unget;              // pushed-back character, or -1 
    unsigned char  sbuf[8];            // fallback when malloc fails 
    struct _FILE  *next;
};

void __stdio_flush_all(void);

#endif