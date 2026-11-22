#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"
#include "auth.h"
#include "envutil.h"

// /etc/sudoers: one user name per line (# starts a comment); those users may run anything
static int sudo_allowed(const char *user) {
    static char buf[1024];
    if (read_whole("/etc/sudoers", buf, sizeof(buf)) < 0) return 0;

    char *line = buf;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int last = (*eol == '\0');
        *eol = '\0';

        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s && *s != '#') {
            char *e = s;
            while (*e && *e != ' ' && *e != '\t' && *e != '\r') e++;
            *e = '\0';
            if (streq(s, user)) return 1;
        }

        if (last) break;
        line = eol + 1;
    }
    return 0;
}

static int has_slash(const char *s) {
    for (; *s; s++)
        if (*s == '/') return 1;
    return 0;
}

static void join(char *dst, unsigned int max, const char *dir, const char *name) {
    unsigned int i = 0;
    while (*dir && i < max - 1) dst[i++] = *dir++;
    while (*name && i < max - 1) dst[i++] = *name++;
    dst[i] = '\0';
}

int main(int argc, char **argv) {
    const char *target_name = "root";
    int i = 1;

    if (i + 1 < argc && streq(argv[i], "-u")) {
        target_name = argv[i + 1];
        i += 2;
    }
    if (i >= argc) {
        puts("usage: sudo [-u USER] COMMAND [ARGUMENT...]\n");
        return 1;
    }

    if (geteuid() != 0) {
        puts("sudo: must be installed setuid root\n");
        return 1;
    }

    struct passwd me, target;
    if (pw_lookup(0, (unsigned int)getuid(), &me) < 0) {
        puts("sudo: you do not exist in the passwd database\n");
        return 1;
    }
    if (pw_lookup(target_name, 0, &target) < 0) {
        puts("sudo: unknown user: ");
        puts(target_name);
        puts("\n");
        return 1;
    }

    if (getuid() != 0) {                    // root needs neither a password nor an entry
        if (!sudo_allowed(me.name)) {
            puts(me.name);
            puts(" is not in the sudoers file.\n");
            return 1;
        }

        char prompt[80];
        join(prompt, sizeof(prompt), "[sudo] password for ", me.name);
        join(prompt + ustrlen(prompt), (unsigned int)(sizeof(prompt) - ustrlen(prompt)), ": ", "");

        int ok = 0;
        for (int tries = 0; tries < 3 && !ok; tries++) {
            char pass[128];
            int n = read_password(prompt, pass, sizeof(pass));

            ok = n > 0 && check_password(me.name, pass);
            wipe(pass, sizeof(pass));
            if (!ok && tries < 2) puts("Sorry, try again.\n");
        }
        if (!ok) {
            puts("sudo: 3 incorrect password attempts\n");
            return 1;
        }
    }

    if (setgid(target.gid) < 0 || setuid(target.uid) < 0) {
        puts("sudo: cannot change identity\n");
        return 1;
    }

    char store[512];
    char *envp[8];
    make_env(&target, store, sizeof(store), envp);

    char **cmd = argv + i;
    char path[96];

    if (has_slash(cmd[0])) {
        execve(cmd[0], cmd, envp);
    } else {
        join(path, sizeof(path), "/bin/", cmd[0]);
        execve(path, cmd, envp);
        join(path, sizeof(path), "/sbin/", cmd[0]);
        execve(path, cmd, envp);
    }

    puts("sudo: ");
    puts(cmd[0]);
    puts(": command not found\n");
    return 127;
}