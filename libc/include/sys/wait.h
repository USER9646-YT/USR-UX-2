#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#include <sys/types.h>

#define WNOHANG    1
#define WUNTRACED  2      // accepted, but nothing can stop yet: no effect
#define WCONTINUED 8      // same

#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)  0
#define WTERMSIG(s)     0
#define WIFSTOPPED(s)   0
#define WSTOPSIG(s)     0
#define WIFCONTINUED(s) 0

pid_t wait(int *);
pid_t waitpid(pid_t, int *, int);
#endif