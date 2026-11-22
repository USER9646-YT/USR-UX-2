#include <unistd.h>
#include "ulib.h"
#include "auth.h"
#include "envutil.h"

int main(int argc, char **argv) {
    int login_shell = 0, i = 1;
    const char *target = "root";

    if (i < argc && (streq(argv[i], "-") || streq(argv[i], "-l"))) {
        login_shell = 1;
        i++;
    }
    if (i < argc) target = argv[i];

    struct passwd pw;
    if (pw_lookup(target, 0, &pw) < 0) {
        puts("su: unknown user: ");
        puts(target);
        puts("\n");
        return 1;
    }

    if (geteuid() != 0) {
        puts("su: must be installed setuid root\n");
        return 1;
    }

    // only root (the real user) may switch without a password
    if (getuid() != 0) {
        char pass[128];
        int n = read_password("Password: ", pass, sizeof(pass));
        int ok = n > 0 && check_password(target, pass);

        wipe(pass, sizeof(pass));
        if (!ok) {
            puts("su: authentication failure\n");
            return 1;
        }
    }

    if (setgid(pw.gid) < 0 || setuid(pw.uid) < 0) {
        puts("su: cannot change identity\n");
        return 1;
    }
    if (login_shell && chdir(pw.home) < 0) chdir("/");

    char store[512];
    char *envp[8];
    make_env(&pw, store, sizeof(store), envp);

    char *shell_argv[2] = { pw.shell, 0 };
    execve(pw.shell, shell_argv, envp);

    puts("su: cannot execute ");
    puts(pw.shell);
    puts("\n");
    return 1;
}