#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <pty.h>

int posix_openpt(int flags) {
    return open("/dev/ptmx", flags);
}

int unlockpt(int fd) {
    int zero = 0;
    return ioctl(fd, TIOCSPTLCK, &zero);
}

// ownership is set by the kernel when the slave is created, so there is nothing to do;
// the ioctl only checks that fd really is a pty master
int grantpt(int fd) {
    int n;
    return ioctl(fd, TIOCGPTN, &n);
}

// returns 0 or an errno value (POSIX style)
int ptsname_r(int fd, char *buf, size_t len) {
    char tmp[32];
    char dig[12];
    int n, nd = 0;
    size_t i = 9;
    unsigned v;

    if (ioctl(fd, TIOCGPTN, &n) < 0) return errno;

    memcpy(tmp, "/dev/pts/", 9);
    v = (unsigned)n;
    do { dig[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (nd) tmp[i++] = dig[--nd];
    tmp[i] = '\0';

    if (i + 1 > len) return ERANGE;
    memcpy(buf, tmp, i + 1);
    return 0;
}

char *ptsname(int fd) {
    static char buf[32];
    int r = ptsname_r(fd, buf, sizeof(buf));
    if (r) { errno = r; return NULL; }
    return buf;
}

int openpty(int *amaster, int *aslave, char *name,
            const struct termios *termp, const struct winsize *winp) {
    char sn[32];
    int m, s, r;

    m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return -1;

    if (grantpt(m) < 0 || unlockpt(m) < 0) { close(m); return -1; }
    r = ptsname_r(m, sn, sizeof(sn));
    if (r) { errno = r; close(m); return -1; }

    s = open(sn, O_RDWR | O_NOCTTY);
    if (s < 0) { close(m); return -1; }

    if (termp) tcsetattr(s, TCSANOW, termp);
    if (winp)  ioctl(s, TIOCSWINSZ, winp);

    *amaster = m;
    *aslave  = s;
    if (name) strcpy(name, sn);
    return 0;
}

// makes fd the terminal of this process: new process group (which becomes the foreground
// group of the pty, so ^C reaches it), then fd -> stdin/stdout/stderr
int login_tty(int fd) {
    setpgid(0, 0);
    ioctl(fd, TIOCSCTTY, 0);
    if (dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) return -1;
    if (fd > 2) close(fd);
    return 0;
}

pid_t forkpty(int *amaster, char *name,
              const struct termios *termp, const struct winsize *winp) {
    int m, s;
    pid_t pid;

    if (openpty(&m, &s, name, termp, winp) < 0) return -1;

    pid = fork();
    if (pid < 0) { close(m); close(s); return -1; }
    if (pid == 0) {                     // child: the slave becomes its terminal
        close(m);
        if (login_tty(s) < 0) _exit(1);
        return 0;
    }
    close(s);
    *amaster = m;
    return pid;
}