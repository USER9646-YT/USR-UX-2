#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <pty.h>

pid_t waitpid(pid_t, int *, int);

static int fails;

static void check(const char *what, int ok) {
    printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) fails++;
}

int main(void) {
    char buf[128], name[32];
    int m, s, n;
    struct winsize ws;
    struct termios t, saved;

    check("openpty", openpty(&m, &s, name, NULL, NULL) == 0);
    printf("slave is %s\n", name);
    check("isatty(master)", isatty(m) == 1);
    check("isatty(slave)", isatty(s) == 1);

    check("default window size 80x24",
          ioctl(s, TIOCGWINSZ, &ws) == 0 && ws.ws_col == 80 && ws.ws_row == 24);
    ws.ws_col = 100;
    ioctl(m, TIOCSWINSZ, &ws);
    ws.ws_col = 0;
    check("window size set on master is seen on slave",
          ioctl(s, TIOCGWINSZ, &ws) == 0 && ws.ws_col == 100);

    /* ---- cooked input: master -> slave ---- */
    check("master write", write(m, "hello\r", 6) == 6);
    n = read(s, buf, sizeof(buf));
    check("slave reads \"hello\\n\" (CR became NL)", n == 6 && memcmp(buf, "hello\n", 6) == 0);
    n = read(m, buf, sizeof(buf));
    check("master gets the echo \"hello\\r\\n\"", n == 7 && memcmp(buf, "hello\r\n", 7) == 0);

    write(m, "abc\177d\r", 6);                       // 0x7F = backspace
    n = read(s, buf, sizeof(buf));
    check("backspace edits the line", n == 4 && memcmp(buf, "abd\n", 4) == 0);
    read(m, buf, sizeof(buf));                       // drop the echo

    /* ---- output: slave -> master ---- */
    write(s, "x\n", 2);
    n = read(m, buf, sizeof(buf));
    check("slave output: \\n becomes \\r\\n", n == 3 && memcmp(buf, "x\r\n", 3) == 0);

    /* ---- raw mode ---- */
    check("tcgetattr", tcgetattr(s, &t) == 0);
    saved = t;
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    check("tcsetattr raw", tcsetattr(s, TCSANOW, &t) == 0);
    write(m, "ab", 2);
    n = read(s, buf, sizeof(buf));
    check("raw read returns at once, no newline needed", n == 2 && memcmp(buf, "ab", 2) == 0);
    check("tcsetattr back to cooked", tcsetattr(s, TCSANOW, &saved) == 0);

    /* ---- ^D ---- */
    write(m, "\004", 1);
    check("^D on an empty line = end of file", read(s, buf, sizeof(buf)) == 0);

    /* ---- hangup ---- */
    close(m);
    check("slave read after master closed = EOF", read(s, buf, sizeof(buf)) == 0);
    check("slave write after master closed = EIO",
          write(s, "x", 1) == -1 && errno == EIO);
    close(s);

    /* ---- forkpty: a child whose stdout is the pty ---- */
    int fm, total = 0, st = 0;
    pid_t pid = forkpty(&fm, NULL, NULL, NULL);
    if (pid == 0) {
        write(1, "child says hi\n", 14);
        _exit(0);
    }
    while ((n = read(fm, buf + total, sizeof(buf) - 1 - total)) > 0) total += n;
    check("forkpty: child output arrives on master",
          total == 15 && memcmp(buf, "child says hi\r\n", 15) == 0);
    check("master read after child exit = EIO", n == -1 && errno == EIO);
    check("child exited with 0", waitpid(pid, &st, 0) == pid && st == 0);
    close(fm);

    /* ---- ^C reaches the foreground job ---- */
    pid = forkpty(&fm, NULL, NULL, NULL);
    if (pid == 0) {
        sleep(60);
        _exit(1);
    }
    sleep(1);                                        // let the child set up its terminal
    write(fm, "\003", 1);
    st = 0;
    check("^C kills the job", waitpid(pid, &st, 0) == pid && st != 0);
    close(fm);

    printf("%d failure(s)\n", fails);
    return fails != 0;
}