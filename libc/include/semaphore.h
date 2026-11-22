#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
#include <time.h>

#define SEM_VALUE_MAX 32767
#define SEM_FAILED    ((sem_t *)0)

typedef struct {
    unsigned int value;
    unsigned int magic;
} sem_t;

int sem_init(sem_t *, int, unsigned int);
int sem_destroy(sem_t *);
int sem_wait(sem_t *);
int sem_trywait(sem_t *);
int sem_timedwait(sem_t *, const struct timespec *);
int sem_post(sem_t *);
int sem_getvalue(sem_t *, int *);

// named semaphores need shared memory between processes: not available
sem_t *sem_open(const char *, int, ...);
int    sem_close(sem_t *);
int    sem_unlink(const char *);
#endif