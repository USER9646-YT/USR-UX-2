#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "proc.h"
#include "gdt.h"
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include "elf.h"
#include "vfs.h"
#include "tty.h"
#include "pit.h"
#include "panic.h"
#include "kprintf.h"

#define MAX_PROCS        32
#define KSTACK_SIZE      8192
#define USER_STACK_TOP   0x80000000u
#define USER_STACK_PAGES 64
#define MAX_ARGS         16
#define MAX_ENV          32
#define ARG_BYTES        3072            // argument and environment strings together 

static struct process procs[MAX_PROCS];
struct process *current;
static int      next_pid = 1;
static uint32_t boot_ksp;

extern void switch_context(uint32_t *old_ksp, uint32_t new_ksp);
extern void trap_return(void);

// ---------- helpers ---------- //

static struct process *alloc_proc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == P_UNUSED) {
            memset(&procs[i], 0, sizeof(procs[i]));
            procs[i].pid   = next_pid++;
            procs[i].pgid  = procs[i].pid;
            procs[i].state = P_EMBRYO;
            return &procs[i];
        }
    }
    return NULL;
}

static void release_proc(struct process *p) {
    kfree(p->kstack);
    p->kstack = NULL;
    p->state  = P_UNUSED;
}

static struct process *find_proc(int pid) {
    for (int i = 0; i < MAX_PROCS; i++)
        if (procs[i].pid == pid && procs[i].state != P_UNUSED && procs[i].state != P_EMBRYO)
            return &procs[i];
    return NULL;
}

// The process name is the last component of the program path 
static void set_name(struct process *p, const char *path) {
    const char *base = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' && c[1]) base = c + 1;

    size_t i = 0;
    while (base[i] && i < sizeof(p->name) - 1) {
        p->name[i] = base[i];
        i++;
    }
    p->name[i] = '\0';
}

// Prepares the kernel stack so that the first switch to `p` "returns" into
// trap_return, which pops the trap frame and irets into user mode.
static void setup_kstack(struct process *p, const struct registers *tf) {
    uint32_t *top = (uint32_t *)(p->kstack + KSTACK_SIZE);
    struct registers *ctf = (struct registers *)top - 1;
    *ctf = *tf;

    uint32_t *sp = (uint32_t *)ctf;
    *--sp = (uint32_t)(uintptr_t)trap_return;     // popped by 'ret' in switch_context 
    *--sp = 0;                                    // ebp 
    *--sp = 0;                                    // ebx 
    *--sp = 0;                                    // esi 
    *--sp = 0;                                    // edi 

    // a clean x87 state (what fninit leaves) for the first switch_context into this process 
    sp -= 27;                                     // 108 bytes 
    memset(sp, 0, 108);
    sp[0] = 0x037F;                               // control word: all exceptions masked 
    sp[2] = 0xFFFF;                               // tag word: every register empty

    p->ksp = (uint32_t)(uintptr_t)sp;
}

static void set_tss_stack(struct process *p) {
    gdt_set_kernel_stack((uint32_t)(uintptr_t)(p->kstack + KSTACK_SIZE));
}

int copy_str_from_user(char *dst, const char *src, size_t max) {
    for (size_t i = 0; i < max; i++) {
        if (!paging_check_user((uint32_t)(uintptr_t)(src + i), 1, 0)) return -EFAULT;
        dst[i] = src[i];
        if (!dst[i]) return (int)i;
    }
    return -ENAMETOOLONG;
}

// Copies a NULL-terminated vector of strings (argv or envp) from user space into
// `buf` (packed strings); offs[i] is where string i starts.
static int collect_vec(char *const *uvec, int max, char *buf, uint32_t *used,
                       uint32_t *offs, int *count) {
    int n = 0;

    if (uvec) {
        for (;;) {
            if (n == max) return -E2BIG;
            if (!paging_check_user((uint32_t)(uintptr_t)(uvec + n), sizeof(char *), 0))
                return -EFAULT;

            const char *p = uvec[n];
            if (!p) break;

            int len = copy_str_from_user(buf + *used, p, ARG_BYTES - *used);
            if (len < 0) return len == -ENAMETOOLONG ? -E2BIG : len;

            offs[n++] = *used;
            *used += (uint32_t)len + 1;
        }
    }
    *count = n;
    return 0;
}

// Maps a fresh user stack into `dir` and writes argc, argv and envp on it.
// offs[0 .. argc-1] are the arguments, offs[argc .. argc+envc-1] the environment.
static int build_user_stack(uint32_t dir, int argc, int envc, const char *strs,
                            const uint32_t *offs, uint32_t *esp_out) {
    uint32_t top_frame = 0;

    for (uint32_t i = 0; i < USER_STACK_PAGES; i++) {
        uint32_t frame = pmm_alloc();
        if (!frame) return -ENOMEM;
        memset((void *)frame, 0, PAGE_SIZE);
        if (paging_map_in(dir, USER_STACK_TOP - (i + 1) * PAGE_SIZE, frame,
                          PTE_USER | PTE_WRITE) != 0) {
            pmm_free(frame);
            return -ENOMEM;
        }
        if (i == 0) top_frame = frame;
    }

    // Everything is written into the top page; sp is an offset inside it.
    uint8_t *page = (uint8_t *)top_frame;
    const uint32_t base = USER_STACK_TOP - PAGE_SIZE;     // virtual address of that page
    uint32_t va[MAX_ARGS + MAX_ENV];
    uint32_t sp = PAGE_SIZE;
    int total = argc + envc;

    for (int i = total - 1; i >= 0; i--) {
        size_t len = strlen(strs + offs[i]) + 1;
        sp -= (uint32_t)len;
        memcpy(page + sp, strs + offs[i], len);
        va[i] = base + sp;
    }

    sp &= ~15u;
    sp -= (uint32_t)(1 + argc + 1 + envc + 1) * 4;   // argc, argv[], NULL, envp[], NULL
    sp &= ~15u;

    uint32_t *w = (uint32_t *)(page + sp);
    int k = 0;
    w[k++] = (uint32_t)argc;
    for (int i = 0; i < argc; i++) w[k++] = va[i];
    w[k++] = 0;                                       // end of argv
    for (int i = 0; i < envc; i++) w[k++] = va[argc + i];
    w[k++] = 0;                                       // end of envp

    *esp_out = base + sp;
    return 0;
}

// ---------- scheduler ---------- //

void schedule(void) {
    struct process *prev = current;
    struct process *next = NULL;

    for (;;) {
        int start = (int)(prev - procs);
        for (int i = 1; i <= MAX_PROCS; i++) {
            struct process *p = &procs[(start + i) % MAX_PROCS];
            if (p->state == P_RUNNABLE) { next = p; break; }
        }
        if (next) break;

        // Nothing to run: wait for an interrupt (keyboard, timer) to wake someone
        __asm__ volatile ("sti; hlt; cli");
    }

    if (next == prev) { prev->state = P_RUNNING; return; }

    current     = next;
    vfs_set_creds(next->uid, next->gid);
    next->state = P_RUNNING;
    set_tss_stack(next);
    paging_switch(next->dir);
    switch_context(&prev->ksp, next->ksp);
}

void yield(void) {
    current->state = P_RUNNABLE;
    schedule();
}

void sched_sleep(enum proc_state why) {
    current->state = why;
    schedule();
}

void sched_wake(enum proc_state why) {
    for (int i = 0; i < MAX_PROCS; i++)
        if (procs[i].state == why) procs[i].state = P_RUNNABLE;
}

// ---------- creating the first process ---------- //

int proc_spawn_init(const char *path) {
    struct process *p = alloc_proc();
    if (!p) return -EAGAIN;

    uint32_t entry, esp, brk;
    char strs[64] = "init\0PATH=/bin:/sbin\0TERM=linux";     // argv[0], then two environment strings
    uint32_t offs[3] = { 0, 5, 21 };
    int r;

    p->kstack = kmalloc(KSTACK_SIZE);
    p->dir    = paging_create_space();
    if (!p->kstack || !p->dir) { r = -ENOMEM; goto fail; }

    r = elf_load(path, p->dir, &entry, &brk);
    if (r) goto fail;
    r = build_user_stack(p->dir, 1, 2, strs, offs, &esp);
    if (r) goto fail;

    struct registers tf;
    memset(&tf, 0, sizeof(tf));
    tf.gs = tf.fs = tf.es = tf.ds = 0x23;
    tf.eip      = entry;
    tf.cs       = 0x1B;
    tf.eflags   = 0x202;                     // IF set
    tf.useresp  = esp;
    tf.ss       = 0x23;

    setup_kstack(p, &tf);

    for (int i = 0; i < 3; i++)
        p->fds[i] = vfs_file_alloc(tty_console(), O_RDWR);
    p->cwd[0] = '/';
    p->cwd[1] = '\0';
    p->brk_start = p->brk_end = brk;
    set_name(p, "init");

    p->state = P_RUNNABLE;
    return 0;

fail:
    if (p->dir) paging_destroy_space(p->dir);
    release_proc(p);
    return r;
}

void proc_start(void) {
    struct process *p = NULL;
    for (int i = 0; i < MAX_PROCS; i++)
        if (procs[i].state == P_RUNNABLE) { p = &procs[i]; break; }

    if (!p) {
        kprintf("proc: nothing to run\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    __asm__ volatile ("fninit");              // a defined x87 state before the first switch

    current  = p;
    vfs_set_creds(p->uid, p->gid);
    p->state = P_RUNNING;
    set_tss_stack(p);
    paging_switch(p->dir);
    switch_context(&boot_ksp, p->ksp);        // never comes back

    for (;;) __asm__ volatile ("hlt");
}

// ---------- fork / exec / exit / wait ---------- //

int proc_fork(struct registers *r) {
    struct process *c = alloc_proc();
    if (!c) return -EAGAIN;

    c->kstack = kmalloc(KSTACK_SIZE);
    c->dir    = paging_clone_space(current->dir);
    if (!c->kstack || !c->dir) {
        if (c->dir) paging_destroy_space(c->dir);
        release_proc(c);
        return -ENOMEM;
    }

    struct registers tf = *r;
    tf.eax = 0;                               // fork returns 0 in the child 
    setup_kstack(c, &tf);

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (current->fds[i]) {
            vfs_file_ref(current->fds[i]);
            c->fds[i] = current->fds[i];
        }
    }
    memcpy(c->cwd, current->cwd, sizeof(c->cwd));
    memcpy(c->name, current->name, sizeof(c->name));
    c->uid       = current->uid;
    c->ruid      = current->ruid;
    c->gid       = current->gid;
    c->pgid      = current->pgid;             // the child stays in the parent's group 
    c->brk_start = current->brk_start;
    c->brk_end   = current->brk_end;

    c->ppid  = current->pid;
    c->state = P_RUNNABLE;
    return c->pid;
}

int proc_exec(struct registers *r, const char *upath, char *const *uargv, char *const *uenvp) {
    char *path = kmalloc(VFS_PATH_MAX);
    char *strs = kmalloc(ARG_BYTES);
    uint32_t offs[MAX_ARGS + MAX_ENV];
    uint32_t used = 0;
    uint32_t dir = 0, entry = 0, esp = 0, brk = 0, old;
    int argc = 0, envc = 0, ret, n;
    struct vfs_stat st;

    if (!path || !strs) { ret = -ENOMEM; goto out; }

    n = copy_str_from_user(path, upath, VFS_PATH_MAX);
    if (n < 0) { ret = n; goto out; }

    ret = collect_vec(uargv, MAX_ARGS, strs, &used, offs, &argc);
    if (ret) goto out;

    if (argc == 0) {                          // no argv given: use the path 
        memcpy(strs, path, (size_t)n + 1);
        offs[0] = 0;
        used = (uint32_t)n + 1;
        argc = 1;
    }

    ret = collect_vec(uenvp, MAX_ENV, strs, &used, offs + argc, &envc);
    if (ret) goto out;

    ret = vfs_access_path(path, ACC_X);       // may this user run it? 
    if (ret) goto out;
    ret = vfs_stat(path, &st);
    if (ret) goto out;

    dir = paging_create_space();
    if (!dir) { ret = -ENOMEM; goto out; }

    // Build the new image first, so a failure leaves the old program intact 
    ret = elf_load(path, dir, &entry, &brk);
    if (ret) goto fail;
    ret = build_user_stack(dir, argc, envc, strs, offs, &esp);
    if (ret) goto fail;

    old = current->dir;
    current->dir = dir;
    paging_switch(dir);
    paging_destroy_space(old);

    set_name(current, path);
    current->brk_start = current->brk_end = brk;

    // setuid / setgid executables run as the file's owner / group 
    if (st.mode & 04000) current->uid = st.uid;
    if (st.mode & 02000) current->gid = st.gid;
    vfs_set_creds(current->uid, current->gid);

    __asm__ volatile ("fninit");              // the new program starts with a clean x87 

    r->eip     = entry;
    r->useresp = esp;
    r->eflags  = 0x202;
    r->eax = r->ebx = r->ecx = r->edx = 0;
    r->esi = r->edi = r->ebp = 0;

    kfree(path);
    kfree(strs);
    return 0;

fail:
    paging_destroy_space(dir);
out:
    kfree(path);
    kfree(strs);
    return ret;
}

void proc_exit(int code) {
    struct process *p = current;

    // if (p->pid == 1) kpanic("attempted to kill init! (exit code %d)", code);

    if (p->pid == 1) {
    kprintf("nusois got baited n shieet mayne\n");
    kpanic("attempted to kill init! (exit code %d)", code);
    }

    p->exit_code = code;

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        vfs_file_put(p->fds[i]);
        p->fds[i] = NULL;
    }

    // Leave the dying address space, then free it 
    paging_switch(paging_kernel_dir());
    paging_destroy_space(p->dir);
    p->dir = 0;

    // Orphans become children of init 
    for (int i = 0; i < MAX_PROCS; i++)
        if (procs[i].state != P_UNUSED && procs[i].state != P_EMBRYO &&
            procs[i].ppid == p->pid)
            procs[i].ppid = 1;

    p->state = P_ZOMBIE;
    sched_wake(P_SLEEP_WAIT);                 // the parent (or init) may be waiting 
    schedule();

    for (;;) __asm__ volatile ("hlt");        // never reached 
}

int proc_waitpid(int pid, int *status, int options) {
    for (;;) {
        int have_child = 0;

        for (int i = 0; i < MAX_PROCS; i++) {
            struct process *q = &procs[i];
            if (q->state == P_UNUSED || q->state == P_EMBRYO) continue;
            if (q->ppid != current->pid) continue;
            if (pid > 0 && q->pid != pid) continue;

            have_child = 1;
            if (q->state == P_ZOMBIE) {
                int cpid = q->pid;
                *status = (q->exit_code & 0xFF) << 8;
                release_proc(q);
                return cpid;
            }
        }

        if (!have_child) return -ECHILD;
        if (options & 1) return 0;            // WNOHANG: children exist, none has exited 

        sched_sleep(P_SLEEP_WAIT);
        if (current->killed) return -EINTR;
    }
}

// ---------- signals, process groups, sleeping, process list ---------- //

static void deliver(struct process *p, int sig) {
    p->killed = sig;                              // every implemented signal terminates 

    if (p->state == P_SLEEP_TTY || p->state == P_SLEEP_WAIT ||
        p->state == P_SLEEP_PIPE || p->state == P_SLEEP_TIMER)
        p->state = P_RUNNABLE;                    // wake it so it notices 
}

int proc_signal(int pid, int sig) {
    if (sig < 0 || sig > 31 || pid <= 0) return -EINVAL;

    struct process *p = find_proc(pid);
    if (!p || p->state == P_ZOMBIE) return -ESRCH;

    if (current->uid != 0 && current->uid != p->uid && current->ruid != p->uid)
        return -EPERM;
    if (sig == 0) return 0;                       // just checking that it exists 

    // init is protected, with one exception: root's SIGKILL kills it, and the kernel panics.
    // (This is what the killinit test program uses.)
    if (pid == 1 && !(sig == 9 && current->uid == 0)) return -EPERM;

    if (sig == 17 || sig == 18) return 0;         // SIGCHLD, SIGCONT: ignored 
    if (sig > 15) return -EINVAL;                 // stop / real-time signals: not implemented 

    deliver(p, sig);
    return 0;
}

// Used by the terminal for Ctrl-C: no permission checks, and safe in interrupt context 
int proc_kill_group(int pgid, int sig) {
    int n = 0;

    if (pgid <= 0 || sig <= 0 || sig > 15) return 0;

    for (int i = 0; i < MAX_PROCS; i++) {
        struct process *p = &procs[i];
        if (p->state == P_UNUSED || p->state == P_EMBRYO || p->state == P_ZOMBIE) continue;
        if (p->pgid != pgid || p->pid == 1) continue;

        deliver(p, sig);
        n++;
    }
    return n;
}

int proc_setpgid(int pid, int pgid) {
    struct process *p = (pid == 0 || pid == current->pid) ? current : find_proc(pid);

    if (!p) return -ESRCH;
    if (p != current && p->ppid != current->pid) return -EPERM;   // only ourselves and our children 
    if (pgid < 0) return -EINVAL;

    p->pgid = pgid == 0 ? p->pid : pgid;
    return 0;
}

int proc_getpgid(int pid) {
    struct process *p = (pid == 0) ? current : find_proc(pid);
    return p ? p->pgid : -ESRCH;
}

void proc_tick(uint32_t ticks) {
    for (int i = 0; i < MAX_PROCS; i++)
        if (procs[i].state == P_SLEEP_TIMER &&
            (int32_t)(ticks - procs[i].wake_tick) >= 0)
            procs[i].state = P_RUNNABLE;
}

int proc_sleep(uint32_t nticks) {
    current->wake_tick = pit_ticks() + nticks;

    while (!current->killed && (int32_t)(pit_ticks() - current->wake_tick) < 0)
        sched_sleep(P_SLEEP_TIMER);

    return current->killed ? -EINTR : 0;
}

int proc_info(int index, struct procinfo *out) {
    if (index < 0 || index >= MAX_PROCS) return -EINVAL;

    struct process *p = &procs[index];
    if (p->state == P_UNUSED || p->state == P_EMBRYO) return 0;

    out->pid  = p->pid;
    out->ppid = p->ppid;
    out->uid  = (int)p->uid;
    switch (p->state) {
    case P_RUNNABLE:
    case P_RUNNING: out->state = 'R'; break;
    case P_ZOMBIE:  out->state = 'Z'; break;
    default:        out->state = 'S'; break;
    }
    memcpy(out->name, p->name, sizeof(out->name));
    return 1;
}