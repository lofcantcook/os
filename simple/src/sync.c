/*
 * sync.c - Named semaphore + process-shared mutex.
 *
 * POSIX calls used:
 *   sem_open()     create a named semaphore that several processes can use
 *   sem_post()     add 1 to the semaphore ("one more chunk is done")
 *   sem_wait()     wait until the value is > 0, then subtract 1
 *   sem_close()    close this process's handle
 *   sem_unlink()   delete the semaphore name from the system
 *
 *   pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)
 *                  allow a mutex to be used by different PROCESSES, not
 *                  only threads. The mutex itself must be in shared memory.
 *   pthread_mutex_lock() / pthread_mutex_unlock()
 *                  enter / leave the critical section
 *   pthread_mutex_destroy()
 *                  free the mutex when nobody uses it any more
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sync.h"

int sync_create(sync_state_t *s, progress_t *progress)
{
    snprintf(s->sem_name, sizeof(s->sem_name), "/sort_sem_%d", (int)getpid());
    sem_unlink(s->sem_name);   /* remove a leftover from an old crash */

    /* Start value 0: nothing has been sorted yet. */
    s->all_sorted = sem_open(s->sem_name, O_CREAT, 0600, 0);
    if (s->all_sorted == SEM_FAILED) {
        perror("sem_open");
        return -1;
    }

    /* Set up the mutex so it works across fork()ed processes. */
    s->progress = progress;
    s->progress->chunks_done = 0;
    s->progress->elements_sorted = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    int rc = pthread_mutex_init(&s->progress->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    if (rc != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(rc));
        sem_close(s->all_sorted);
        sem_unlink(s->sem_name);
        return -1;
    }
    return 0;
}

void sync_chunk_done(sync_state_t *s, size_t elements)
{
    /* ---- critical section: only one process at a time ---- */
    pthread_mutex_lock(&s->progress->lock);
    s->progress->chunks_done++;
    s->progress->elements_sorted += elements;
    pthread_mutex_unlock(&s->progress->lock);
    /* ------------------------------------------------------- */

    /* Tell the parent that one more chunk is ready. */
    if (sem_post(s->all_sorted) == -1)
        perror("sem_post");
}

int sync_wait_all(sync_state_t *s, int n)
{
    for (int i = 0; i < n; i++) {
        /* sem_wait() blocks until some child has called sem_post().
         * If a signal interrupts it (EINTR) we just wait again. */
        while (sem_wait(s->all_sorted) == -1) {
            if (errno != EINTR) {
                perror("sem_wait");
                return -1;
            }
        }
    }
    return 0;
}

void sync_close(sync_state_t *s)
{
    sem_close(s->all_sorted);
}

void sync_remove(sync_state_t *s)
{
    pthread_mutex_destroy(&s->progress->lock);
    sem_close(s->all_sorted);
    sem_unlink(s->sem_name);
}
