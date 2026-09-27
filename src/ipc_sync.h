/*
 * ipc_sync.h - Synchronization for the merge phase.
 *
 * - A named POSIX semaphore (sem_open) counts finished chunks. Each worker
 *   posts once after sorting; the parent waits N times before merging.
 * - A pthread_mutex_t with PTHREAD_PROCESS_SHARED, stored in shared memory,
 *   protects progress counters that every worker updates.
 */
#ifndef IPC_SYNC_H
#define IPC_SYNC_H

#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <sys/types.h>

#define SEM_NAME_LEN 64

/* Lives inside a MAP_SHARED mapping so all processes see the same copy. */
typedef struct {
    pthread_mutex_t lock;
    size_t chunks_done;
    size_t elems_sorted;
    int    failures;
} shared_progress_t;

typedef struct {
    sem_t *sorted;               /* counts sorted chunks */
    char   sem_name[SEM_NAME_LEN];
    shared_progress_t *progress; /* points into shared memory */
} ipc_sync_t;

/*
 * Parent: create the named semaphore and initialise the process-shared
 * mutex inside `shared_mem`, which must be at least
 * sizeof(shared_progress_t) bytes of MAP_SHARED memory.
 */
int  ipc_sync_create(ipc_sync_t *s, void *shared_mem);

/* Parent: destroy the mutex, close and unlink the semaphore. */
void ipc_sync_destroy(ipc_sync_t *s);

/* Worker: close the inherited semaphore handle before exiting. */
void ipc_sync_close(ipc_sync_t *s);

/* Worker: record one finished chunk and signal the parent. */
int  ipc_sync_chunk_done(ipc_sync_t *s, size_t elems, int failed);

/*
 * Parent: block until `n` chunks are signalled. Every `poll_sec` seconds
 * `alive_cb(arg)` is called; if it returns non-zero (e.g. a worker died)
 * the wait is aborted and -1 is returned. alive_cb may be NULL.
 */
int  ipc_sync_wait_all(ipc_sync_t *s, int n, int poll_sec,
                       int (*alive_cb)(void *), void *arg);

#endif /* IPC_SYNC_H */
