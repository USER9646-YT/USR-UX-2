#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

pid_t waitpid(pid_t, int *, int);

#define LOG "/tmp/nx.log"

static int logfd = -1;

static void say(const char *s) {
    if (logfd >= 0) {
        write(logfd, s, strlen(s));
        sync();                          // keep the log even if the machine hangs afterwards
    }
}

static pid_t spawn(const char *path) {
    char line[96];
    pid_t pid = fork();

    if (pid == 0) {
        char *argv[2];
        argv[0] = (char *)path;
        argv[1] = NULL;
        if (logfd >= 0) {                // server and client messages go to the log
            dup2(logfd, 1);
            dup2(logfd, 2);
        }
        execv(path, argv);
        _exit(127);
    }
    snprintf(line, sizeof(line), "startnx: started %s as pid %d\n", path, (int)pid);
    say(line);
    return pid;
}

// startnx [client ...]   (default client: /bin/nxterm)
int main(int argc, char **argv) {
    char line[96];

    logfd = open(LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    pid_t server = spawn("/bin/nano-X");
    if (server < 0) {
        printf("startnx: fork failed\n");
        return 1;
    }
    usleep(1000000);

    if (access("/bin/nanowm", F_OK) == 0) {
        spawn("/bin/nanowm");
        usleep(500000);
    }

    if (argc < 2) {
        spawn("/bin/nxterm");
    } else {
        for (int i = 1; i < argc; i++) spawn(argv[i]);
    }

    for (;;) {                            // report every child that exits
        int st = 0;
        pid_t p = waitpid(-1, &st, 0);
        if (p < 0) break;
        snprintf(line, sizeof(line), "startnx: pid %d exited, status 0x%x\n", (int)p, st);
        say(line);
        if (p == server) break;
    }
    return 0;
}