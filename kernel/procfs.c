#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/procinfo.h>
#include "procfs.h"
#include "vfs.h"
#include "proc.h"
#include "pit.h"
#include "rtc.h"
#include "pmm.h"
#include "kheap.h"
#include "mount.h"

extern uint32_t pmm_total_frames(void);       // defined next to pmm_alloc

enum {
    K_ROOT = 1, K_VERSION, K_UPTIME, K_MEMINFO, K_CPUINFO, K_STAT, K_MOUNTS,
    K_PIDDIR, K_PID_STATUS, K_PID_COMM
};

// A procfs node: the vfs_node must stay the first member, so kfree(node) frees it all
struct pn {
    struct vfs_node node;
    int             kind;
    int             pid;
};

static struct vfs_ops procfs_ops;

// The generated text of one file. The kernel never preempts itself, and a read
// fills and copies this buffer without sleeping, so one shared buffer is safe.
static char gen_buf[4096];

// ---------- a tiny text builder (the kernel has no snprintf) ---------- //

struct sb {
    char    *b;
    uint32_t cap, len;
};

static void sb_chr(struct sb *s, char c) {
    if (s->len + 1 < s->cap) s->b[s->len++] = c;
}

static void sb_str(struct sb *s, const char *str) {
    while (*str) sb_chr(s, *str++);
}

static void sb_uint(struct sb *s, uint32_t v) {
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) sb_chr(s, tmp[--n]);
}

static void sb_uint_pad(struct sb *s, uint32_t v, int width) {   // right-aligned
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = n; i < width; i++) sb_chr(s, ' ');
    while (n) sb_chr(s, tmp[--n]);
}

static void sb_kv(struct sb *s, const char *key, uint32_t v, const char *unit) {
    sb_str(s, key);
    sb_str(s, ":");
    sb_uint_pad(s, v, 10);
    if (unit) { sb_chr(s, ' '); sb_str(s, unit); }
    sb_chr(s, '\n');
}

// ---------- information sources ---------- //

// Finds a live process by pid; 1 = found
static int find_info(int pid, struct procinfo *out) {
    for (int i = 0; i < 256; i++) {
        int r = proc_info(i, out);
        if (r < 0) break;                          // past the end of the process table
        if (r > 0 && out->pid == pid) return 1;
    }
    return 0;
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    /* ebx is saved by hand: it may be reserved when code is compiled as PIC */
    __asm__ volatile ("mov %%ebx, %%esi\n\t"
                      "cpuid\n\t"
                      "xchg %%ebx, %%esi"
                      : "=a"(*a), "=S"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(0)
                      : "memory");
}

static void put_regs(struct sb *s, uint32_t r1, uint32_t r2, uint32_t r3, int skip_spaces) {
    uint32_t regs[3] = { r1, r2, r3 };
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 4; k++) {
            char c = (char)(regs[i] >> (k * 8));
            if (skip_spaces && c == ' ' && s->len > 0 && s->b[s->len - 1] == ':') continue;
            if (c) sb_chr(s, c);
        }
}

struct flag { int bit; const char *name; };

static const struct flag flags_edx[] = {
    {0, "fpu"}, {1, "vme"}, {2, "de"}, {3, "pse"}, {4, "tsc"}, {5, "msr"}, {6, "pae"},
    {7, "mce"}, {8, "cx8"}, {9, "apic"}, {11, "sep"}, {12, "mtrr"}, {13, "pge"},
    {14, "mca"}, {15, "cmov"}, {16, "pat"}, {17, "pse36"}, {19, "clflush"}, {23, "mmx"},
    {24, "fxsr"}, {25, "sse"}, {26, "sse2"}, {28, "ht"}, {-1, NULL}
};

static const struct flag flags_ecx[] = {
    {0, "pni"}, {9, "ssse3"}, {13, "cx16"}, {19, "sse4_1"}, {20, "sse4_2"},
    {23, "popcnt"}, {28, "avx"}, {31, "hypervisor"}, {-1, NULL}
};

// ---------- the files ---------- //

static int gen_version(struct sb *s) {
    sb_str(s, "USR/UX version 2 (i386) #1\n");
    return 0;
}

static int gen_uptime(struct sb *s) {
    uint32_t hz = pit_freq() ? pit_freq() : 100;
    uint32_t ticks = pit_ticks();

    sb_uint(s, ticks / hz);
    sb_chr(s, '.');
    uint32_t cs = (ticks % hz) * 100 / hz;
    sb_chr(s, (char)('0' + cs / 10));
    sb_chr(s, (char)('0' + cs % 10));
    sb_str(s, " 0.00\n");                         // second number (idle time) is not tracked yet
    return 0;
}

static int gen_meminfo(struct sb *s) {
    uint32_t total = pmm_total_frames() * (PAGE_SIZE / 1024);
    uint32_t free  = pmm_free_frames()  * (PAGE_SIZE / 1024);

    sb_kv(s, "MemTotal",     total, "kB");
    sb_kv(s, "MemFree",      free,  "kB");
    sb_kv(s, "MemAvailable", free,  "kB");
    sb_kv(s, "MemUsed",      total - free, "kB");
    sb_kv(s, "PageSize",     PAGE_SIZE, "bytes");
    return 0;
}

static int gen_cpuinfo(struct sb *s) {
    uint32_t a, b, c, d, maxleaf, maxext;

    cpuid(0, &maxleaf, &b, &c, &d);
    sb_str(s, "processor\t: 0\n");
    sb_str(s, "vendor_id\t: ");
    put_regs(s, b, d, c, 0);                      // the vendor string is ebx, edx, ecx
    sb_chr(s, '\n');

    cpuid(0x80000000u, &maxext, &b, &c, &d);
    if (maxext >= 0x80000004u) {
        sb_str(s, "model name\t: ");
        for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
            cpuid(leaf, &a, &b, &c, &d);
            // skip the padding spaces some CPUs put in front of the name
            uint32_t regs[4] = { a, b, c, d };
            for (int i = 0; i < 4; i++)
                for (int k = 0; k < 4; k++) {
                    char ch = (char)(regs[i] >> (k * 8));
                    if (!ch) continue;
                    if (ch == ' ' && s->b[s->len - 1] == ' ') continue;
                    if (ch == ' ' && s->b[s->len - 1] == '\t') continue;
                    if (ch == ' ' && s->b[s->len - 1] == ':') continue;
                    sb_chr(s, ch);
                }
        }
        sb_chr(s, '\n');
    }

    uint32_t family = 0, model = 0, step = 0, feat_edx = 0, feat_ecx = 0;
    if (maxleaf >= 1) {
        cpuid(1, &a, &b, &c, &d);
        step   = a & 15;
        model  = (a >> 4) & 15;
        family = (a >> 8) & 15;
        if (family == 15) family += (a >> 20) & 0xFF;
        if ((((a >> 8) & 15) == 6) || (((a >> 8) & 15) == 15)) model |= ((a >> 16) & 15) << 4;
        feat_edx = d;
        feat_ecx = c;
    }
    sb_str(s, "cpu family\t: "); sb_uint(s, family); sb_chr(s, '\n');
    sb_str(s, "model\t\t: ");    sb_uint(s, model);  sb_chr(s, '\n');
    sb_str(s, "stepping\t: ");   sb_uint(s, step);   sb_chr(s, '\n');

    sb_str(s, "flags\t\t:");
    for (const struct flag *f = flags_edx; f->name; f++)
        if (feat_edx & (1u << f->bit)) { sb_chr(s, ' '); sb_str(s, f->name); }
    for (const struct flag *f = flags_ecx; f->name; f++)
        if (feat_ecx & (1u << f->bit)) { sb_chr(s, ' '); sb_str(s, f->name); }
    sb_chr(s, '\n');
    return 0;
}

static int gen_stat(struct sb *s) {
    struct procinfo pi;
    uint32_t total = 0, running = 0, sleeping = 0, zombie = 0;

    for (int i = 0; i < 256; i++) {
        int r = proc_info(i, &pi);
        if (r < 0) break;
        if (r == 0) continue;
        total++;
        if (pi.state == 'R')      running++;
        else if (pi.state == 'Z') zombie++;
        else                      sleeping++;
    }

    uint32_t hz = pit_freq() ? pit_freq() : 100;
    uint32_t up = pit_ticks() / hz;
    uint32_t now, usec;
    time_now(&now, &usec);

    sb_str(s, "procs_total ");    sb_uint(s, total);    sb_chr(s, '\n');
    sb_str(s, "procs_running ");  sb_uint(s, running);  sb_chr(s, '\n');
    sb_str(s, "procs_sleeping "); sb_uint(s, sleeping); sb_chr(s, '\n');
    sb_str(s, "procs_zombie ");   sb_uint(s, zombie);   sb_chr(s, '\n');
    sb_str(s, "btime ");          sb_uint(s, now - up); sb_chr(s, '\n');
    sb_str(s, "ticks ");          sb_uint(s, pit_ticks()); sb_chr(s, '\n');
    sb_str(s, "hz ");             sb_uint(s, hz);       sb_chr(s, '\n');
    return 0;
}

static int gen_mounts(struct sb *s) {
    char path[VFS_PATH_MAX];
    char dev[24];

    for (int i = 0; vfs_mount_path(i, path) == 0; i++) {
        const char *name = "none";
        const char *type = mount_fstype(path);
        if (!type) type = "none";

        if (mount_device(path, dev, sizeof(dev)) == 0) name = dev;

        sb_str(s, name);  sb_chr(s, ' ');
        sb_str(s, path);  sb_chr(s, ' ');
        sb_str(s, type);  sb_str(s, " rw 0 0\n");
    }
    return 0;
}

static int gen_pid_status(struct sb *s, int pid) {
    struct procinfo pi;
    if (!find_info(pid, &pi)) return -ESRCH;

    const char *state = pi.state == 'R' ? "R (running)" :
                        pi.state == 'Z' ? "Z (zombie)" : "S (sleeping)";

    sb_str(s, "Name:\t");  sb_str(s, pi.name);              sb_chr(s, '\n');
    sb_str(s, "State:\t"); sb_str(s, state);                sb_chr(s, '\n');
    sb_str(s, "Pid:\t");   sb_uint(s, (uint32_t)pi.pid);    sb_chr(s, '\n');
    sb_str(s, "PPid:\t");  sb_uint(s, (uint32_t)pi.ppid);   sb_chr(s, '\n');
    sb_str(s, "Uid:\t");   sb_uint(s, (uint32_t)pi.uid);    sb_chr(s, '\n');
    return 0;
}

static int gen_pid_comm(struct sb *s, int pid) {
    struct procinfo pi;
    if (!find_info(pid, &pi)) return -ESRCH;

    sb_str(s, pi.name);
    sb_chr(s, '\n');
    return 0;
}

// ---------- nodes ---------- //

static struct vfs_node *make_node(int kind, int pid) {
    struct pn *p = kcalloc(1, sizeof(*p));
    if (!p) return NULL;

    int isdir = (kind == K_ROOT || kind == K_PIDDIR);

    p->kind = kind;
    p->pid  = pid;
    p->node.ino  = (kind == K_ROOT) ? 1
                 : (pid > 0) ? 0x1000u + (uint32_t)pid * 16u + (uint32_t)kind
                 : 0x100u + (uint32_t)kind;
    p->node.type = isdir ? VFS_DIR : VFS_FILE;
    p->node.mode = isdir ? (S_IFDIR | 0555) : (S_IFREG | 0444);
    p->node.refs = 1;
    p->node.ops  = &procfs_ops;
    p->node.priv = p;
    return &p->node;
}

static void procfs_release(struct vfs_node *n) {
    kfree(n);                                     // n is the start of the struct pn
}

static int procfs_unmount(struct vfs_node *root) {
    if (root->refs > 1) return -EBUSY;
    vfs_node_put(root);                                 // -> procfs_release
    return 0;
}

static int procfs_read(struct vfs_node *n, uint32_t off, uint32_t len, void *buf) {
    struct pn *p = n->priv;
    struct sb s = { gen_buf, sizeof(gen_buf), 0 };
    int r;

    switch (p->kind) {
    case K_VERSION:    r = gen_version(&s);              break;
    case K_UPTIME:     r = gen_uptime(&s);               break;
    case K_MEMINFO:    r = gen_meminfo(&s);              break;
    case K_CPUINFO:    r = gen_cpuinfo(&s);              break;
    case K_STAT:       r = gen_stat(&s);                 break;
    case K_PID_STATUS: r = gen_pid_status(&s, p->pid);   break;
    case K_PID_COMM:   r = gen_pid_comm(&s, p->pid);     break;
    case K_MOUNTS:     r = gen_mounts(&s);               break;
    default:           return -EISDIR;
    }
    if (r) return r;

    if (off >= s.len) return 0;
    if (len > s.len - off) len = s.len - off;
    memcpy(buf, gen_buf + off, len);
    return (int)len;
}

// ---------- directories ---------- //

struct entry { const char *name; int kind; };

static const struct entry root_entries[] = {
    { "version", K_VERSION }, { "uptime", K_UPTIME }, { "meminfo", K_MEMINFO },
    { "cpuinfo", K_CPUINFO }, { "stat", K_STAT },     { "mounts", K_MOUNTS },
    { "self", K_PIDDIR },
};
#define NROOT ((uint32_t)(sizeof(root_entries) / sizeof(root_entries[0])))

static const struct entry pid_entries[] = {
    { "status", K_PID_STATUS }, { "comm", K_PID_COMM },
};
#define NPID ((uint32_t)(sizeof(pid_entries) / sizeof(pid_entries[0])))

static void fill_dirent(struct vfs_dirent *out, const char *name, int kind, uint32_t ino) {
    size_t n = strlen(name);
    memcpy(out->name, name, n + 1);
    out->ino  = ino;
    out->type = (kind == K_PIDDIR) ? VFS_DIR : VFS_FILE;
}

static int procfs_readdir(struct vfs_node *dir, uint32_t index, struct vfs_dirent *out) {
    struct pn *d = dir->priv;

    if (d->kind == K_PIDDIR) {
        if (index >= NPID) return 0;
        fill_dirent(out, pid_entries[index].name, pid_entries[index].kind, 0);
        return 1;
    }

    if (index < NROOT) {
        fill_dirent(out, root_entries[index].name, root_entries[index].kind, 0x100u + index);
        return 1;
    }

    // after the fixed entries: one directory per live process
    uint32_t want = index - NROOT, seen = 0;
    struct procinfo pi;
    for (int i = 0; i < 256; i++) {
        int r = proc_info(i, &pi);
        if (r < 0) break;
        if (r == 0) continue;
        if (seen++ != want) continue;

        char num[12];
        int n = 0;
        uint32_t v = (uint32_t)pi.pid;
        do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        for (int k = 0; k < n; k++) out->name[k] = num[n - 1 - k];
        out->name[n] = '\0';
        out->ino  = 0x1000u + (uint32_t)pi.pid * 16u;
        out->type = VFS_DIR;
        return 1;
    }
    return 0;
}

static struct vfs_node *procfs_finddir(struct vfs_node *dir, const char *name) {
    struct pn *d = dir->priv;
    struct procinfo pi;

    if (d->kind == K_PIDDIR) {
        if (!find_info(d->pid, &pi)) return NULL;        // the process is gone
        for (uint32_t i = 0; i < NPID; i++)
            if (strcmp(name, pid_entries[i].name) == 0)
                return make_node(pid_entries[i].kind, d->pid);
        return NULL;
    }

    if (strcmp(name, "self") == 0) return make_node(K_PIDDIR, current->pid);

    for (uint32_t i = 0; i < NROOT; i++)
        if (root_entries[i].kind != K_PIDDIR && strcmp(name, root_entries[i].name) == 0)
            return make_node(root_entries[i].kind, 0);

    /* a process id */
    int pid = 0;
    size_t len = strlen(name);
    if (len == 0 || len > 9) return NULL;
    for (size_t i = 0; i < len; i++) {
        if (name[i] < '0' || name[i] > '9') return NULL;
        pid = pid * 10 + (name[i] - '0');
    }
    if (pid <= 0 || !find_info(pid, &pi)) return NULL;
    return make_node(K_PIDDIR, pid);
}

static struct vfs_ops procfs_ops = {
    .read    = procfs_read,
    .readdir = procfs_readdir,
    .finddir = procfs_finddir,
    .release = procfs_release,
    .unmount = procfs_unmount,
};

struct vfs_node *procfs_create(void) {
    return make_node(K_ROOT, 0);
}