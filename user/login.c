#include <unistd.h>
#include <fcntl.h>
#include "ulib.h"
#include "auth.h"
#include "envutil.h"
#include "kernel/syscall.h"

static int read_line(char *buf, int max) {
    int n = read(0, buf, (unsigned int)(max - 1));
    if (n <= 0) return -1;
    if (buf[n - 1] == '\n') n--;
    buf[n] = '\0';
    return n;
}

int main(void) {
    char user[256], pass[256];

    for (;;) {
        struct utsname u;
        uname(&u);
        puts("\nUSR/UX "
             OS_RELEASE);
        puts("\n");
        puts(u.nodename);
        puts(" login: ");
        if (read_line(user, sizeof(user)) <= 0) continue;

        puts("Password: ");
        ioctl(0, TIOCSECHO, 0);
        int n = read_line(pass, sizeof(pass));
        ioctl(0, TIOCSECHO, 1);
        if (n <= 0) continue;                       // empty or interrupted

        struct passwd pw;
        if (ustrlen(user) >= sizeof(pw.name) ||
            pw_lookup(user, 0, &pw) < 0 ||
            !check_password(user, pass)) {
            wipe(pass, sizeof(pass));
            puts("Login incorrect\n");
            continue;
        }
        wipe(pass, sizeof(pass));

        int pid = fork();
        if (pid == 0) {
            if (setgid(pw.gid) < 0 || setuid(pw.uid) < 0) {   // gid first: after setuid we can't
                puts("login: cannot change identity\n");
                _exit(1);
            }
            if (chdir(pw.home) < 0) chdir("/");

            char store[512];
            char *envp[8];
            make_env(&pw, store, sizeof(store), envp);

            char *argv[2] = { pw.shell, 0 };
            execve(pw.shell, argv, envp);
            puts("login: cannot execute ");
            puts(pw.shell);
            puts("\n");
            _exit(1);
        }

        int status;
        waitpid(pid, &status, 0);
    }
}