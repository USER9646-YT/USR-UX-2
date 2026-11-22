#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>

pid_t waitpid(pid_t, int *, int);

int main(void) {
    char buf[256];
    char *argv[2];

    int m = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m < 0 || grantpt(m) < 0 || unlockpt(m) < 0) {
        printf("ptyshell: pty setup failed, errno %d\n", errno);
        return 1;
    }
    char *name = ptsname(m);
    if (!name) {
        printf("ptyshell: ptsname failed\n");
        return 1;
    }
    printf("ptyshell: slave is %s. Type exit to leave.\n", name);

    pid_t pid = fork();
    if (pid < 0) {
        printf("ptyshell: fork failed\n");
        return 1;
    }
    if (pid == 0) {                       // the same steps as nxterm's child
        argv[0] = "/bin/sh";
        argv[1] = NULL;
        close(0);
        close(1);
        close(m);
        setsid();
        int s = open(name, O_RDWR);
        if (s < 0) _exit(1);
        ioctl(s, TIOCSCTTY, 0);
        close(2);
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2) close(s);
        execv("/bin/sh", argv);
        _exit(127);
    }

    ioctl(0, TIOCSRAW, 1);                // keys go straight to the pty, which echoes them
    for (;;) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(0, &r);
        FD_SET(m, &r);
        if (select(m + 1, &r, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(m, &r)) {
            int n = read(m, buf, sizeof(buf));
            if (n > 0) write(1, buf, n);
            else if (n == 0 || (errno != EAGAIN && errno != EINTR)) break;   // the shell is gone
        }
        if (FD_ISSET(0, &r)) {
            int n = read(0, buf, sizeof(buf));
            if (n > 0) write(m, buf, n);
        }
    }
    ioctl(0, TIOCSRAW, 0);

    int st = 0;
    waitpid(pid, &st, 0);
    printf("\nptyshell: shell exited, status 0x%x\n", st);
    return 0;
}