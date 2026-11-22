#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "ulib.h"

#ifndef WNOHANG
#define WNOHANG 1
#endif

#define MAX_ARGS 15
#define MAX_CMDS 8

struct cmd {
    char *argv[MAX_ARGS + 1];
    int   argc;
    char *in;               // < file  
    char *out;              // > file or >> file 
    int   append;
};

// what ended a pipeline on the command line
enum { SEP_END, SEP_SEQ, SEP_AND, SEP_OR, SEP_BG };

static struct cmd cmds[MAX_CMDS];
static char wordbuf[256];   // all words of one pipeline, with quotes removed 
static int  wordpos;
static char scriptbuf[4096];
static char **env;          // the environment every program we start receives 
static int  interactive;    // a person is typing: use job control 

static void cmd_init(struct cmd *c) {
    c->argc    = 0;
    c->argv[0] = 0;
    c->in      = 0;
    c->out     = 0;
    c->append  = 0;
}

// Copies the word at *pp into wordbuf (removing quotes). Returns it, or 0 if there is none. 
static char *parse_word(char **pp) {
    char *s = *pp;
    char *begin = s;
    char *start = wordbuf + wordpos;
    char quote = 0;
    int n = 0;

    while (*s) {
        if (quote) {
            if (*s == quote) { quote = 0; s++; continue; }
        } else {
            if (*s == '"' || *s == '\'') { quote = *s++; continue; }
            if (*s == ' ' || *s == '|' || *s == '<' || *s == '>' ||
                *s == '&' || *s == ';') break;
        }
        if (wordpos + n >= (int)sizeof(wordbuf) - 1) break;
        start[n++] = *s++;
    }
    start[n] = '\0';
    wordpos += n + 1;
    *pp = s;
    return s != begin ? start : 0;
}

// Parses one pipeline from *pp, up to a separator (; && || &) or the end of the line.
// Returns the number of commands in it, 0 if it is empty, -1 on a syntax error.
// *sep says what ended it, and *pp is moved past that separator.
static int parse_pipeline(char **pp, int *sep) {
    int nc = 0;
    char *p = *pp;

    cmd_init(&cmds[0]);
    wordpos = 0;
    *sep = SEP_END;

    for (;;) {
        while (*p == ' ') p++;
        if (!*p) break;

        if (*p == '&' && p[1] == '&') { p += 2; *sep = SEP_AND; break; }
        if (*p == '|' && p[1] == '|') { p += 2; *sep = SEP_OR;  break; }
        if (*p == '&')                { p++;    *sep = SEP_BG;  break; }
        if (*p == ';')                { p++;    *sep = SEP_SEQ; break; }

        if (*p == '|') {
            p++;
            if (cmds[nc].argc == 0 || nc + 1 >= MAX_CMDS) return -1;
            nc++;
            cmd_init(&cmds[nc]);
            continue;
        }

        if (*p == '<' || *p == '>') {
            char op = *p++;
            int append = 0;
            if (op == '>' && *p == '>') { append = 1; p++; }
            while (*p == ' ') p++;

            char *w = parse_word(&p);
            if (!w) return -1;
            if (op == '<') {
                cmds[nc].in = w;
            } else {
                cmds[nc].out = w;
                cmds[nc].append = append;
            }
            continue;
        }

        char *w = parse_word(&p);
        if (!w) return -1;
        if (cmds[nc].argc >= MAX_ARGS) return -1;
        cmds[nc].argv[cmds[nc].argc++] = w;
        cmds[nc].argv[cmds[nc].argc] = 0;
    }

    *pp = p;
    if (cmds[nc].argc == 0) return nc == 0 ? 0 : -1;     // empty pipeline, or a trailing '|' 
    return nc + 1;
}

static int has_slash(const char *s) {
    for (; *s; s++)
        if (*s == '/') return 1;
    return 0;
}

static void join_path(char *dst, unsigned int max, const char *dir, const char *name) {
    unsigned int i = 0;
    while (*dir && i < max - 1) dst[i++] = *dir++;
    while (*name && i < max - 1) dst[i++] = *name++;
    dst[i] = '\0';
}

// Builds an absolute, normalised path in dst: a relative name (./prog, ../bin/prog, dir/prog)
// is taken from the current directory, and "." and ".." components are resolved.
static int make_absolute(char *dst, unsigned int max, const char *name) {
    unsigned int len = 0;

    if (name[0] != '/') {
        if (getcwd(dst, max) <= 0) return -1;
        while (dst[len]) len++;
        if (len == 1) len = 0;                      // the cwd is "/": the components add their own slash 
    }

    const char *p = name;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        const char *c = p;
        while (*p && *p != '/') p++;
        unsigned int cl = (unsigned int)(p - c);

        if (cl == 1 && c[0] == '.') continue;
        if (cl == 2 && c[0] == '.' && c[1] == '.') {
            while (len > 0 && dst[len - 1] != '/') len--;
            if (len > 0) len--;
            continue;
        }
        if (len + 1 + cl >= max) return -1;
        dst[len++] = '/';
        for (unsigned int i = 0; i < cl; i++) dst[len++] = c[i];
    }
    if (len == 0) dst[len++] = '/';
    dst[len] = '\0';
    return 0;
}

// Runs a program: the path as typed (relative ones like ./prog are made absolute first),
// or /bin/<name>, then /sbin/<name>. Only returns if the program could not be started;
// the result is the error (-errno) that says why.
static int exec_command(char **argv) {
    char path[128];
    int r;

    if (has_slash(argv[0])) {
        if (make_absolute(path, sizeof(path), argv[0]) < 0) return -ENAMETOOLONG;
        return execve(path, argv, env);
    }

    join_path(path, sizeof(path), "/bin/", argv[0]);
    r = execve(path, argv, env);
    if (r != -ENOENT) return r;                     // it exists but can't run: say so, don't hide it 

    join_path(path, sizeof(path), "/sbin/", argv[0]);
    return execve(path, argv, env);
}

static void set_foreground(int pgrp) {
    ioctl(0, TIOCSPGRP, (unsigned int)&pgrp);       // 0 = no foreground job 
}

// Runs a built-in command in the shell itself. Returns 1 if argv[0] was one (its exit
// status goes to *status), 0 if it is an ordinary program.
static int builtin(struct cmd *c, int *status) {
    char **argv = c->argv;

    *status = 0;

    if (streq(argv[0], "exit")) {
        int code = 0;
        if (c->argc > 1)
            for (const char *s = argv[1]; *s >= '0' && *s <= '9'; s++) code = code * 10 + (*s - '0');
        _exit(code);
    }
    if (streq(argv[0], "pid")) {
        print_int(getpid());
        puts("\n");
        return 1;
    }
    if (streq(argv[0], "help")) {
        puts("ls cat echo cp mv rm mkdir rmdir touch grep head tail wc tee vi env\n"
             "clear sleep ps kill pkill chmod chown uname hostname id whoami su sudo\n"
             "true false halt poweroff reboot telinit runlevel cc hello\n"
             "help pid cd pwd exit\n"
             "operators: | < > >> && || ; & (& runs a command in the background)\n");
        return 1;
    }
    if (streq(argv[0], "cd")) {
        const char *target = c->argc > 1 ? argv[1] : "/";
        int r = chdir(target);
        if (r < 0) {
            eprint("cd", target, r);
            *status = 1;
        }
        return 1;
    }
    if (streq(argv[0], "pwd")) {
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd)) > 0) { puts(cwd); puts("\n"); }
        return 1;
    }
    return 0;
}

// Runs a pipeline of n commands. In the foreground it waits and returns the wait status of
// the last command; in the background it returns at once (with 0).
static int run_pipeline(int n, int background) {
    int pids[MAX_CMDS];
    int started = 0;
    int prev = -1;                       // read end of the previous pipe 
    int jobpg = 0;                       // process group of this command line 

    for (int i = 0; i < n; i++) {
        int pfd[2] = { -1, -1 };

        if (i < n - 1 && pipe(pfd) < 0) {
            puts("fush: cannot create pipe\n");
            break;
        }

        int pid = fork();
        if (pid < 0) {
            puts("fush: fork failed\n");
            if (pfd[0] >= 0) { close(pfd[0]); close(pfd[1]); }
            break;
        }

        if (pid == 0) {
            if (interactive) setpgid(0, jobpg);     // 0 for the first command: a new group of its own 

            if (prev >= 0) {
                dup2(prev, 0);
                close(prev);
            } else if (background) {                // a background job must not read the keyboard
                int nul = open("/dev/null", O_RDONLY);
                if (nul >= 0) { dup2(nul, 0); close(nul); }
            }
            if (pfd[1] >= 0) { dup2(pfd[1], 1); close(pfd[1]); }
            if (pfd[0] >= 0) close(pfd[0]);

            if (cmds[i].in) {
                int fd = open(cmds[i].in, O_RDONLY);
                if (fd < 0) { eprint("fush", cmds[i].in, fd); _exit(1); }
                dup2(fd, 0);
                close(fd);
            }
            if (cmds[i].out) {
                int fd = open(cmds[i].out,
                              O_WRONLY | O_CREAT | (cmds[i].append ? O_APPEND : O_TRUNC));
                if (fd < 0) { eprint("fush", cmds[i].out, fd); _exit(1); }
                dup2(fd, 1);
                close(fd);
            }

            int r = exec_command(cmds[i].argv);
            puts("fush: ");
            if (r == -ENOENT || r == -ENOTDIR) puts("command not found: ");
            else if (r == -EACCES)             puts("permission denied: ");
            else if (r == -ENOEXEC)            puts("not a runnable program: ");
            else if (r == -E2BIG)              puts("argument list too long: ");
            else                               puts("cannot execute: ");
            puts(cmds[i].argv[0]);
            puts("\n");
            _exit(r == -ENOENT || r == -ENOTDIR ? 127 : 126);
        }

        if (interactive) {                          // the parent sets it too, so there is no race 
            if (jobpg == 0) {
                jobpg = pid;
                setpgid(pid, jobpg);
                if (!background) set_foreground(jobpg);     // Ctrl-C now reaches this job 
            } else {
                setpgid(pid, jobpg);
            }
        }

        pids[started++] = pid;
        if (prev >= 0)   close(prev);    // the parent keeps no copies: otherwise nobody sees EOF 
        if (pfd[1] >= 0) close(pfd[1]);
        prev = pfd[0];
    }
    if (prev >= 0) close(prev);

    if (background) {
        if (started > 0) {
            puts("[");
            print_int(pids[started - 1]);
            puts("] started\n");
        }
        return 0;                                   // not waited for: reap_jobs() collects it later
    }

    int status = 0;
    for (int i = 0; i < started; i++) {
        int st;
        waitpid(pids[i], &st, 0);
        if (i == started - 1) status = st;
    }

    if (interactive) set_foreground(0);
    return status;
}

// Collects background jobs that have finished (so they don't stay zombies) and says so.
// Every foreground job has already been waited for, so these can only be background jobs.
static void reap_jobs(void) {
    int st;
    int pid;

    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        puts("[");
        print_int(pid);
        puts("] done\n");
    }
}

// Runs one pipeline and returns its exit status (a background one counts as 0).
static int run_segment(int nc, int background) {
    int status;

    if (nc == 1 && !cmds[0].in && !cmds[0].out && builtin(&cmds[0], &status)) return status;

    int st = run_pipeline(nc, background);
    return background ? 0 : WEXITSTATUS(st);
}

// Runs one command line: pipelines joined by ; && || and &. Returns the exit status of the
// last pipeline that ran.
//   a ; b    run b after a             a && b   run b only if a succeeded
//   a || b   run b only if a failed    a &      run a in the background
static int execute_line(char *line) {
    char *p = line;
    int status = 0;
    int run = 1;                         // does the next pipeline run, given the one before? 
    int prev = SEP_SEQ;

    for (;;) {
        int sep;
        int nc = parse_pipeline(&p, &sep);

        if (nc < 0 || (nc == 0 && sep != SEP_END)) {        // "&& x", ";;", "a |"
            puts("fush: syntax error\n");
            return 2;
        }
        if (nc == 0) {                                      // the end of the line
            if (prev == SEP_AND || prev == SEP_OR) {        // "a &&" with nothing after it
                puts("fush: syntax error\n");
                return 2;
            }
            break;
        }

        if (run) status = run_segment(nc, sep == SEP_BG);

        if (sep == SEP_AND)     run = (status == 0);
        else if (sep == SEP_OR) run = (status != 0);
        else                    run = 1;

        if (sep == SEP_END) break;
        prev = sep;
    }
    return status;
}

static int run_script(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        eprint("fush", path, fd);
        return 127;
    }

    int total = 0, n;
    while (total < (int)sizeof(scriptbuf) - 1 &&
           (n = read(fd, scriptbuf + total, sizeof(scriptbuf) - 1 - (unsigned int)total)) > 0)
        total += n;
    close(fd);
    scriptbuf[total] = '\0';

    int status = 0;
    char *p = scriptbuf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        int last = (*eol == '\0');
        *eol = '\0';

        char line[128];
        int len = 0;
        for (const char *s = p; *s && len < (int)sizeof(line) - 1; s++)
            line[len++] = (*s == '\t' || *s == '\r') ? ' ' : *s;
        line[len] = '\0';

        char *s = line;
        while (*s == ' ') s++;
        if (*s && *s != '#') status = execute_line(s);

        if (last) break;
        p = eol + 1;
    }
    return status;
}

int main(int argc, char **argv, char **envp) {
    char line[128];

    env = envp;

    if (argc >= 3 && streq(argv[1], "-c")) {
        int len = 0;
        for (const char *s = argv[2]; *s && len < (int)sizeof(line) - 1; s++) line[len++] = *s;
        line[len] = '\0';
        return execute_line(line);
    }
    if (argc >= 2)
        return run_script(argv[1]);

    interactive = 1;
    setpgid(0, 0);                                  // the shell gets a process group of its own 
    puts("Fast and Usable Shell (FUSH) v4\n");
    puts("Welcome! Type 'help' for the list of built-in commands.\n");

    for (;;) {
        ioctl(0, TIOCSRAW, 0);                      // recover if a program left the tty in raw mode 
        ioctl(0, TIOCSECHO, 1);
        set_foreground(0);                          // nothing to interrupt while we wait for input 
        reap_jobs();                                // report background jobs that have finished 
        puts(getuid() == 0 ? "fush4-1.0# " : "fush4-1.0$ ");

        int n = read(0, line, sizeof(line) - 1);
        if (n == 0) { puts("exit\n"); _exit(0); }       // Ctrl-D
        if (n < 0) continue;
        if (line[n - 1] == '\n') n--;
        line[n] = '\0';

        execute_line(line);
    }
}