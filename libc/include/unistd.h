#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _SC_ARG_MAX        0
#define _SC_CHILD_MAX      1
#define _SC_CLK_TCK        2
#define _SC_OPEN_MAX       4
#define _SC_PAGESIZE       30
#define _SC_NPROCESSORS_ONLN 84

extern char **environ;
extern char *optarg;
extern int optind, opterr, optopt;

ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
ssize_t pread(int, void *, size_t, off_t);
ssize_t pwrite(int, const void *, size_t, off_t);
int     close(int);
off_t   lseek(int, off_t, int);
int     dup(int);
int     dup2(int, int);
int     pipe(int[2]);
int     access(const char *, int);
int     chdir(const char *);
char   *getcwd(char *, size_t);
int     rmdir(const char *);
int     unlink(const char *);
int     link(const char *, const char *);
int     symlink(const char *, const char *);
ssize_t readlink(const char *, char *, size_t);
int     chown(const char *, uid_t, gid_t);
int     truncate(const char *, off_t);
int     ftruncate(int, off_t);
int     fsync(int);
int     fdatasync(int);
void    sync(void);

pid_t   fork(void);
int     execve(const char *, char *const[], char *const[]);
int     execv(const char *, char *const[]);
int     execvp(const char *, char *const[]);
int     execl(const char *, const char *, ...);
int     execlp(const char *, const char *, ...);
void    _exit(int) __attribute__((noreturn));

pid_t   getpid(void);
pid_t   getppid(void);
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
gid_t   getegid(void);
int     setuid(uid_t);
int     setgid(gid_t);
int     setpgid(pid_t, pid_t);
pid_t   setsid(void);
pid_t   getpgrp(void);
pid_t   getpgid(pid_t);

int     isatty(int);
char   *ttyname(int);
unsigned int sleep(unsigned int);
int     usleep(useconds_t);
unsigned int alarm(unsigned int);
int     pause(void);

int     brk(void *);
void   *sbrk(ptrdiff_t);
long    sysconf(int);
int     getpagesize(void);
int     gethostname(char *, size_t);
int     sethostname(const char *, size_t);
char   *getlogin(void);

int     getopt(int, char *const[], const char *);

pid_t   vfork(void);
pid_t   getsid(pid_t);
int     nice(int);
int     getgroups(int, gid_t *);
int     setgroups(size_t, const gid_t *);
int     setreuid(uid_t, uid_t);
int     setregid(gid_t, gid_t);
int     setresuid(uid_t, uid_t, uid_t);
int     setresgid(gid_t, gid_t, gid_t);
int     getresuid(uid_t *, uid_t *, uid_t *);
int     getresgid(gid_t *, gid_t *, gid_t *);
int     fchown(int, uid_t, gid_t);
int     lchown(const char *, uid_t, gid_t);
int     dup3(int, int, int);
int     pipe2(int[2], int);
#endif