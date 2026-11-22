#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

pid_t waitpid(pid_t, int *, int);

#define SOCK_PATH "/tmp/test.sock"

static int fails;

static void check(const char *what, int ok) {
    printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) fails++;
}

static void make_addr(struct sockaddr_un *a, const char *path) {
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    strcpy(a->sun_path, path);
}

int main(void) {
    char buf[64];
    int sv[2], n;
    struct sockaddr_un addr, other;

    /* ---- socketpair ---- */
    check("socketpair", socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    check("pair: a -> b", write(sv[0], "ping", 4) == 4 &&
          read(sv[1], buf, sizeof(buf)) == 4 && memcmp(buf, "ping", 4) == 0);
    check("pair: b -> a", write(sv[1], "pong", 4) == 4 &&
          read(sv[0], buf, sizeof(buf)) == 4 && memcmp(buf, "pong", 4) == 0);
    check("isatty(socket) is 0", isatty(sv[0]) == 0);

    shutdown(sv[0], SHUT_WR);
    check("pair: EOF after shutdown(WR)", read(sv[1], buf, sizeof(buf)) == 0);
    check("pair: other direction still works",
          write(sv[1], "x", 1) == 1 && read(sv[0], buf, 1) == 1);

    close(sv[1]);
    check("pair: EOF after peer closed", read(sv[0], buf, sizeof(buf)) == 0);
    check("pair: write to closed peer = EPIPE",
          write(sv[0], "x", 1) == -1 && errno == EPIPE);
    close(sv[0]);

    /* ---- listening socket ---- */
    int srv = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    check("socket", srv >= 0);

    make_addr(&addr, SOCK_PATH);
    check("bind", bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    int dup = socket(AF_UNIX, SOCK_STREAM, 0);
    check("bind same path twice = EADDRINUSE",
          bind(dup, (struct sockaddr *)&addr, sizeof(addr)) == -1 && errno == EADDRINUSE);

    make_addr(&other, "/tmp/nothing-here.sock");
    check("connect to missing path = ENOENT",
          connect(dup, (struct sockaddr *)&other, sizeof(other)) == -1 && errno == ENOENT);
    close(dup);

    check("listen", listen(srv, 4) == 0);
    check("accept with nobody there = EAGAIN",
          accept(srv, NULL, NULL) == -1 && errno == EAGAIN);

    /* ---- client in a child process ---- */
    pid_t pid = fork();
    if (pid == 0) {
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        if (connect(c, (struct sockaddr *)&addr, sizeof(addr)) < 0) _exit(10);
        if (write(c, "hello", 5) != 5) _exit(11);
        n = read(c, buf, sizeof(buf));
        if (n != 10 || memcmp(buf, "hello back", 10) != 0) _exit(12);
        close(c);
        _exit(0);
    }

    struct pollfd pfd;
    pfd.fd = srv;
    pfd.events = POLLIN;
    pfd.revents = 0;
    check("poll(listener) reports POLLIN",
          poll(&pfd, 1, 3000) == 1 && (pfd.revents & POLLIN));

    int conn = accept(srv, NULL, NULL);
    check("accept", conn >= 0);

    n = read(conn, buf, sizeof(buf));
    check("server reads \"hello\"", n == 5 && memcmp(buf, "hello", 5) == 0);
    check("server replies", write(conn, "hello back", 10) == 10);

    int st = -1;
    check("client exited with 0", waitpid(pid, &st, 0) == pid && st == 0);
    check("server sees EOF after client exit", read(conn, buf, sizeof(buf)) == 0);

    close(conn);
    close(srv);

    printf("%d failure(s)\n", fails);
    return fails != 0;
}