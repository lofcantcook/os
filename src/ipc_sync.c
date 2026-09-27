/*
 * ipc_sync.c - Named semaphore + process-shared mutex implementation.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ipc_sync.h"

int ipc_sync_create(ipc_sync_t *s, void *shared_mem)
{
    pthread_mutexattr_t attr;
    int rc;

    s->sorted = SEM_FAILED;
    s->progress = (shared_progress_t *)shared_mem;
    snprintf(s->sem_name, sizeof(s->sem_name), "/psort_sorted_%d", (int)getpid());

    sem_unlink(s->sem_name);
    s->sorted = sem_open(s->sem_name, O_CREAT | O_EXCL, 0600, 0);
    if (s->sorted == SEM_FAILED) {
        perror("sem_open");
        s->sem_name[0] = '\0';
        return -1;
    }

    memset(s->progress, 0, sizeof(*s->progress));
    if ((rc = pthread_mutexattr_init(&attr)) != 0)
        goto mutex_fail;
    if ((rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)) != 0 ||
        (rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST)) != 0 ||
        (rc = pthread_mutex_init(&s->progress->lock, &attr)) != 0) {
        pthread_mutexattr_destroy(&attr);
        goto mutex_fail;
    }
    pthread_mutexattr_destroy(&attr);
    return 0;

mutex_fail:
    fprintf(stderr, "pthread_mutex setup: %s\n", strerror(rc));
    sem_close(s->sorted);
    sem_unlink(s->sem_name);
    s->sorted = SEM_FAILED;
    s->sem_name[0] = '\0';
    s->progress = NULL;
    return -1;
}

void ipc_sync_close(ipc_sync_t *s)
{
    if (s->sorted != SEM_FAILED) {
        sem_close(s->sorted);
        s->sorted = SEM_FAILED;
    }
}

void ipc_sync_destroy(ipc_sync_t *s)
{
    if (s->progress) {
        pthread_mutex_destroy(&s->progress->lock);
        s->progress = NULL;
    }
    ipc_sync_close(s);
    if (s->sem_name[0]) {
        sem_unlink(s->sem_name);
        s->sem_name[0] = '\0';
    }
}

/* Lock the robust mutex; recover it if a previous owner died holding it. */
static int lock_progress(shared_progress_t *p)
{
    int rc = pthread_mutex_lock(&p->lock);

    if (rc == EOWNERDEAD) {
        pthread_mutex_consistent(&p->lock);
        return 0;
    }
    return rc;
}

int ipc_sync_chunk_done(ipc_sync_t *s, size_t elems, int failed)
{
    int rc = lock_progress(s->progress);

    if (rc != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(rc));
        return -1;
    }
    s->progress->chunks_done++;
    s->progress->elems_sorted += elems;
    if (failed)
        s->progress->failures++;
    pthread_mutex_unlock(&s->progress->lock);

    if (sem_post(s->sorted) == -1) {
        perror("sem_post");
        return -1;
    }
    return 0;
}

int ipc_sync_wait_all(ipc_sync_t *s, int n, int poll_sec,
                      int (*alive_cb)(void *), void *arg)
{
    int got = 0;

    while (got < n) {
        struct timespec deadline;

        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += poll_sec > 0 ? poll_sec : 1;

        if (sem_timedwait(s->sorted, &deadline) == 0) {
            got++;
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno != ETIMEDOUT) {
            perror("sem_timedwait");
            return -1;
        }
        if (alive_cb && alive_cb(arg) != 0)
            return -1;
    }
    return 0;
}
