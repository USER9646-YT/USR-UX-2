#include <signal.h>
#include <errno.h>
#include <unistd.h>

// Handlers are recorded but never run: the kernel only terminates on signals for now.
static sighandler_t handlers[NSIG];
static sigset_t blocked;

sighandler_t signal(int sig, sighandler_t h) {
    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return SIG_ERR; }
    sighandler_t old = handlers[sig];
    handlers[sig] = h;
    return old;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) { errno = EINVAL; return -1; }
    if (old) {
        old->sa_handler = handlers[sig];
        old->sa_mask = 0;
        old->sa_flags = 0;
    }
    if (act) handlers[sig] = act->sa_handler;
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old) {
    if (old) *old = blocked;
    if (!set) return 0;
    switch (how) {
    case SIG_BLOCK:   blocked |= *set; break;
    case SIG_UNBLOCK: blocked &= ~*set; break;
    case SIG_SETMASK: blocked = *set; break;
    default: errno = EINVAL; return -1;
    }
    return 0;
}

int sigemptyset(sigset_t *s)                   { *s = 0; return 0; }
int sigfillset(sigset_t *s)                    { *s = ~0u; return 0; }
int sigaddset(sigset_t *s, int n)              { if (n <= 0 || n >= NSIG) { errno = EINVAL; return -1; } *s |= 1u << n; return 0; }
int sigdelset(sigset_t *s, int n)              { if (n <= 0 || n >= NSIG) { errno = EINVAL; return -1; } *s &= ~(1u << n); return 0; }
int sigismember(const sigset_t *s, int n)      { return n > 0 && n < NSIG && (*s >> n) & 1; }

int raise(int sig) {
    return kill(getpid(), sig);
}