/*
 * sync.h - Synchronization between the parent and the child processes.
 *
 * 1. Semaphore "all sorted" (sem_open, starts at 0)
 *    Every child calls sem_post() once when its chunk is sorted.
 *    The parent calls sem_wait() N times, so it cannot start the merge
 *    until ALL N children are finished.
 *
 * 2. Mutex (pthread_mutex_t with PTHREAD_PROCESS_SHARED)
 *    Protects a small progress counter that lives in shared memory and is
 *    updated by every child. Without the lock, two children could update
 *    it at the same time and one update would be lost.
 */
#ifndef SYNC_H
#define SYNC_H

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>

/* This struct must be placed in shared memory (mmap with MAP_SHARED). */
typedef struct {
    pthread_mutex_t lock;
    int    chunks_done;
    size_t elements_sorted;
} progress_t;

typedef struct {
    sem_t      *all_sorted;
    char        sem_name[64];
    progress_t *progress;     /* points into shared memory */
} sync_state_t;

/* Parent: create the semaphore and set up the mutex inside `progress`. */
int  sync_create(sync_state_t *s, progress_t *progress);

/* Child: "my chunk of `elements` values is done" (mutex + sem_post). */
void sync_chunk_done(sync_state_t *s, size_t elements);

/* Parent: wait until `n` children have called sync_chunk_done(). */
int  sync_wait_all(sync_state_t *s, int n);

/* Child: close its semaphore handle (before exiting). */
void sync_close(sync_state_t *s);

/* Parent: destroy the mutex and delete the semaphore from the system. */
void sync_remove(sync_state_t *s);

#endif
