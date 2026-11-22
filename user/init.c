#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include "ulib.h"
#include "pwd.h"                // read_whole(), copy_str()

#define MAX_ENTRIES   16
#define MAX_ARGS       8
#define POLL_MS      100
#define POLLS_PER_MIN 600

enum { A_IGNORE, A_SYSINIT, A_BOOT, A_BOOTWAIT, A_WAIT, A_ONCE, A_RESPAWN };

struct entry {
    char id[8];
    char levels[16];
    int  action;
    char cmd[96];
    int  pid;                   // running process, 0 = none
    int  respawns;              // spawns in the current minute
    int  disabled_until;        // poll count until which respawning is suspended
};

static struct entry entries[MAX_ENTRIES];
static int  nentries;
static int  runlevel;           // '0'..'6' or 'S'; 0 until the first runlevel is entered
static int  prev_level = 'N';
static int  default_level = '3';
static int  polls;
static char inittab[4096];

// ---------- /etc/inittab ---------- //

static int action_from_name(const char *s) {
    if (streq(s, "sysinit"))  return A_SYSINIT;
    if (streq(s, "boot"))     return A_BOOT;
    if (streq(s, "bootwait")) return A_BOOTWAIT;
    if (streq(s, "wait"))     return A_WAIT;
    if (streq(s, "once"))     return A_ONCE;
    if (streq(s, "respawn"))  return A_RESPAWN;
    return A_IGNORE;
}

static void add_entry(const char *id, const char *levels, int action, const char *cmd) {
    if (nentries >= MAX_ENTRIES) return;

    struct entry *e = &entries[nentries++];
    copy_str(e->id, id, sizeof(e->id));
    copy_str(e->levels, levels, sizeof(e->levels));
    copy_str(e->cmd, cmd, sizeof(e->cmd));
    e->action = action;
    e->pid = 0;
    e->respawns = 0;
    e->disabled_until = 0;
}

static void parse_entry(char *line) {
    char *f[4];
    int nf = 0;
    char *p = line;

    f[nf++] = p;
    while (*p && nf < 4) {
        if (*p == ':') { *p = '\0'; f[nf++] = p + 1; }
        p++;
    }
    if (nf < 4) return;

    if (streq(f[2], "initdefault")) {
        if (f[1][0]) default_level = f[1][0];
        return;
    }

    int action = action_from_name(f[2]);
    if (action != A_IGNORE) add_entry(f[0], f[1], action, f[3]);
}

static void load_inittab(void) {
    nentries = 0;

    if (read_whole("/etc/inittab", inittab, sizeof(inittab)) < 0) {
        puts("INIT: no /etc/inittab, using built-in defaults\n");
        add_entry("1", "2345", A_RESPAWN, "/bin/login");
        return;
    }

    char *line = inittab;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int last = (*eol == '\0');
        *eol = '\0';

        if (line[0] && line[0] != '#') parse_entry(line);

        if (last) break;
        line = eol + 1;
    }
}

static int in_level(const struct entry *e, int lvl) {
    if (!e->levels[0]) return 1;                // empty = every runlevel

    for (const char *c = e->levels; *c; c++) {
        int a = *c, b = lvl;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a == b) return 1;
    }
    return 0;
}

// ---------- starting and stopping things ---------- //

static int spawn(struct entry *e) {
    int pid = fork();

    if (pid < 0) {
        puts("INIT: fork failed\n");
        return -1;
    }

    if (pid == 0) {
        char buf[96];
        char *argv[MAX_ARGS + 1];
        int argc = 0;

        copy_str(buf, e->cmd, sizeof(buf));
        char *p = buf;
        while (*p && argc < MAX_ARGS) {
            while (*p == ' ') p++;
            if (!*p) break;
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
        argv[argc] = 0;
        if (argc == 0) _exit(127);

        execve(argv[0], argv, 0);
        puts("INIT: cannot execute ");
        puts(argv[0]);
        puts("\n");
        _exit(127);
    }

    e->pid = pid;
    return pid;
}

static void note_exit(int pid) {
    for (int i = 0; i < nentries; i++)
        if (entries[i].pid == pid) entries[i].pid = 0;
}

// Reaps children (remembering which entry they belonged to) until `pid` is gone
static void reap_until(int pid) {
    for (;;) {
        int st;
        int w = waitpid(-1, &st, 0);
        if (w < 0) return;
        note_exit(w);
        if (w == pid) return;
    }
}

static void run_wait(struct entry *e) {
    int pid = spawn(e);
    if (pid > 0) reap_until(pid);
    e->pid = 0;
}

static void reap_children(void) {
    int st, w;
    while ((w = waitpid(-1, &st, WNOHANG)) > 0) note_exit(w);
}

// Kills a process and everything below it
static void kill_tree(int pid) {
    struct procinfo pi;

    for (int i = 0; ; i++) {
        int r = procinfo(i, &pi);
        if (r < 0) break;
        if (r > 0 && pi.ppid == pid && pi.state != 'Z') kill_tree(pi.pid);
    }
    kill(pid, SIGTERM);
}

static void write_runlevel(void) {
    int fd = open("/run/runlevel", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;

    char msg[4] = { (char)prev_level, ' ', (char)runlevel, '\n' };
    write(fd, msg, 4);
    close(fd);
}

static void enter_runlevel(int lvl) {
    char c = (char)lvl;

    puts("INIT: Entering runlevel: ");
    write(1, &c, 1);
    puts("\n");

    // stop the services that don't belong to the new runlevel
    for (int i = 0; i < nentries; i++) {
        struct entry *e = &entries[i];
        if (e->action == A_RESPAWN && e->pid > 0 && !in_level(e, lvl)) {
            kill_tree(e->pid);
            reap_until(e->pid);
            e->pid = 0;
        }
    }

    prev_level = runlevel ? runlevel : 'N';
    runlevel = lvl;
    write_runlevel();

    for (int i = 0; i < nentries; i++) {
        struct entry *e = &entries[i];
        if (!in_level(e, lvl)) continue;

        e->respawns = 0;
        e->disabled_until = 0;
        if (e->action == A_WAIT)      run_wait(e);
        else if (e->action == A_ONCE) spawn(e);
    }
}

// telinit drops the requested runlevel into /run/initctl
static void check_initctl(void) {
    int fd = open("/run/initctl", O_RDONLY);
    if (fd < 0) return;

    char c = 0;
    int n = read(fd, &c, 1);
    close(fd);
    if (n <= 0) return;                         // telinit is still writing: try again next time
    unlink("/run/initctl");

    if (c == 's') c = 'S';
    if (((c >= '0' && c <= '6') || c == 'S') && c != runlevel)
        enter_runlevel(c);
}

static void respawn_pass(void) {
    if (polls % POLLS_PER_MIN == 0)
        for (int i = 0; i < nentries; i++) entries[i].respawns = 0;

    for (int i = 0; i < nentries; i++) {
        struct entry *e = &entries[i];

        if (e->action != A_RESPAWN || e->pid != 0 || !in_level(e, runlevel)) continue;
        if (polls < e->disabled_until) continue;

        if (++e->respawns > 10) {
            puts("INIT: id \"");
            puts(e->id);
            puts("\" respawning too fast: disabled for 5 minutes\n");
            e->disabled_until = polls + 5 * POLLS_PER_MIN;
            e->respawns = 0;
            continue;
        }
        spawn(e);
    }
}

int main(void) {
    puts("\nINIT: version 2 booting\n");
    load_inittab();

    for (int i = 0; i < nentries; i++)
        if (entries[i].action == A_SYSINIT) run_wait(&entries[i]);

    for (int i = 0; i < nentries; i++) {
        if (entries[i].action == A_BOOTWAIT)  run_wait(&entries[i]);
        else if (entries[i].action == A_BOOT) spawn(&entries[i]);
    }

    enter_runlevel(default_level);

    struct timespec ts = { 0, POLL_MS * 1000000 };
    for (;;) {
        check_initctl();
        reap_children();
        respawn_pass();
        nanosleep(&ts, 0);
        polls++;
    }
}