#include <semaphore.h>
#include <errno.h>

#define SEM_MAGIC 0x53454D31u              // "SEM1": catches use of an uninitialised semaphore

// One thread, and processes don't share memory: nobody else can post a semaphore that
// this process waits on. So a wait that would block forever fails instead of hanging.

int sem_init(sem_t *s, int pshared, unsigned int value) {
    if (value > SEM_VALUE_MAX) { errno = EINVAL; return -1; }
    if (pshared)               { errno = ENOSYS; return -1; }   // can't be shared between processes

    s->value = value;
    s->magic = SEM_MAGIC;
    return 0;
}

int sem_destroy(sem_t *s) {
    if (s->magic != SEM_MAGIC) { errno = EINVAL; return -1; }
    s->magic = 0;
    return 0;
}

int sem_trywait(sem_t *s) {
    if (s->magic != SEM_MAGIC) { errno = EINVAL; return -1; }
    if (s->value == 0)         { errno = EAGAIN; return -1; }
    s->value--;
    return 0;
}

int sem_wait(sem_t *s) {
    if (s->magic != SEM_MAGIC) { errno = EINVAL; return -1; }
    if (s->value == 0)         { errno = EDEADLK; return -1; }   // would wait forever
    s->value--;
    return 0;
}

int sem_timedwait(sem_t *s, const struct timespec *abs_timeout) {
    (void)abs_timeout;
    if (s->magic != SEM_MAGIC) { errno = EINVAL; return -1; }
    if (s->value == 0)         { errno = ETIMEDOUT; return -1; } // nobody could post meanwhile
    s->value--;
    return 0;
}

int sem_post(sem_t *s) {
    if (s->magic != SEM_MAGIC)       { errno = EINVAL; return -1; }
    if (s->value >= SEM_VALUE_MAX)   { errno = EOVERFLOW; return -1; }
    s->value++;
    return 0;
}

int sem_getvalue(sem_t *s, int *out) {
    if (s->magic != SEM_MAGIC) { errno = EINVAL; return -1; }
    *out = (int)s->value;
    return 0;
}

sem_t *sem_open(const char *name, int oflag, ...) {
    (void)name; (void)oflag;
    errno = ENOSYS;
    return SEM_FAILED;
}

int sem_close(sem_t *s)           { (void)s; errno = ENOSYS; return -1; }
int sem_unlink(const char *name)  { (void)name; errno = ENOSYS; return -1; }