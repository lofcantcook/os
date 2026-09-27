/*
 * main.c - Parallel merge sort: puts all the pieces together.
 *
 *   1. Get the data into shared memory.
 *   2. Split it into N chunks.
 *   3. Sort the chunks in parallel, with N threads OR N child processes.
 *   4. Merge the N sorted chunks with the min-heap merge (merge.c).
 *   5. Check the result and print PASS or FAIL (not included in the timing).
 *
 * Process mode, step by step:
 *
 *   parent                                  child (one per chunk)
 *   ------                                  ---------------------
 *   fork() N times  ─────────────────────►  receive_task()      (waits)
 *   send_task() N times  ────────────────►  merge_sort(chunk)
 *                                           mutex: update progress counter
 *   sem_wait() N times  ◄────────────────   sem_post()
 *   receive_done() N times  ◄────────────   send_done()
 *   waitpid() every child (no zombies)      exit
 *   merge_chunks()
 *
 * Usage:
 *   parallel_merge_sort [-m thread|process] [-w workers] [-n elements]
 *                       [-s seed] [-r run_number] [-c results.csv]
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "merge.h"
#include "message_queue.h"
#include "sync.h"

/* ------------------------------------------------------------------ */
/* Data setup                                                         */
/* ------------------------------------------------------------------ */

/*
 * Memory that stays shared between the parent and its fork()ed children.
 * MAP_SHARED means a child's writes go to the SAME physical pages the
 * parent sees, instead of copy-on-write private copies.
 *
 * Integration point: this can be swapped for the shm_open() + ftruncate()
 * + mmap(MAP_SHARED) segment from the shared memory module.
 */
static void *shared_alloc(size_t bytes)
{
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

/*
 * Fill the array with pseudo-random numbers (same seed = same numbers).
 *
 * Integration point: this can be swapped for the file I/O module, which
 * writes the dataset with write() and loads it back into shared memory
 * with read().
 */
static void fill_random(int32_t *a, size_t n, unsigned seed)
{
    uint64_t x = 88172645463325252ULL ^ seed;    /* xorshift64 generator */
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        a[i] = (int32_t)(x >> 32);
    }
}

/* ------------------------------------------------------------------ */
/* Thread mode                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t           *data;
    chunk_t            chunk;
    pthread_barrier_t *barrier;
    int                ok;
} thread_arg_t;

static void *thread_main(void *p)
{
    thread_arg_t *arg = p;
    size_t len = arg->chunk.end - arg->chunk.start;

    arg->ok = (merge_sort(arg->data + arg->chunk.start, len) == 0);

    /* Wait here until every thread (and main) reaches the barrier. */
    pthread_barrier_wait(arg->barrier);
    return NULL;
}

static int sort_with_threads(int32_t *data, const chunk_t *chunks, int workers)
{
    pthread_t threads[MAX_WORKERS];
    thread_arg_t args[MAX_WORKERS];
    pthread_barrier_t barrier;
    int ok = 1;

    /* workers + 1 because the main thread also waits at the barrier. */
    pthread_barrier_init(&barrier, NULL, (unsigned)workers + 1);

    for (int i = 0; i < workers; i++) {
        args[i].data = data;
        args[i].chunk = chunks[i];
        args[i].barrier = &barrier;
        int rc = pthread_create(&threads[i], NULL, thread_main, &args[i]);
        if (rc != 0) {
            /* The barrier can never be reached now, so just stop. */
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            exit(1);
        }
    }

    pthread_barrier_wait(&barrier);   /* sort phase -> merge phase */

    for (int i = 0; i < workers; i++) {
        pthread_join(threads[i], NULL);
        if (!args[i].ok)
            ok = 0;
    }
    pthread_barrier_destroy(&barrier);
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Process mode                                                       */
/* ------------------------------------------------------------------ */

/* Code that runs inside each child process. Never returns. */
static void child_main(int32_t *data, queues_t *q, sync_state_t *sync)
{
    task_msg_t task;
    done_msg_t done;

    /* 1. Wait for the parent to tell us which chunk to sort. */
    if (receive_task(q, &task) != 0)
        _exit(1);

    /* 2. Sort it, directly inside the shared memory. */
    size_t len = task.end - task.start;
    double t0 = now_seconds();
    int ok = merge_sort(data + task.start, len) == 0 &&
             is_sorted(data + task.start, len);

    /* 3. Update the shared counter (mutex) and signal the parent
     *    (semaphore). We post BEFORE sending the "done" message: the
     *    parent only reads the done queue after the semaphore, so a full
     *    done queue can never stop a child from posting. No deadlock. */
    sync_chunk_done(sync, ok ? len : 0);

    /* 4. Report back to the parent. */
    done.chunk_id = task.chunk_id;
    done.pid = (int)getpid();
    done.ok = ok;
    done.seconds = now_seconds() - t0;
    send_done(q, &done);

    queues_close(q);
    sync_close(sync);
    _exit(ok ? 0 : 1);   /* _exit: don't run the parent's atexit handlers */
}

static int sort_with_processes(int32_t *data, const chunk_t *chunks,
                               int workers, size_t n, int verbose)
{
    queues_t q;
    sync_state_t sync;
    pid_t pids[MAX_WORKERS];
    int started = 0;
    int ok = 1;

    progress_t *progress = shared_alloc(sizeof(progress_t));
    if (progress == NULL) {
        perror("mmap (progress)");
        return -1;
    }
    if (queues_create(&q) != 0) {
        munmap(progress, sizeof(progress_t));
        return -1;
    }
    if (sync_create(&sync, progress) != 0) {
        queues_remove(&q);
        munmap(progress, sizeof(progress_t));
        return -1;
    }

    /* 1. Start the children. */
    for (int i = 0; i < workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            ok = 0;
            break;
        }
        if (pid == 0)
            child_main(data, &q, &sync);   /* child never returns */
        pids[started++] = pid;
    }

    if (ok) {
        /* 2. Message passing: send each child its chunk boundaries. */
        for (int i = 0; i < workers; i++) {
            task_msg_t task = { i, chunks[i].start, chunks[i].end };
            if (send_task(&q, &task) != 0) {
                ok = 0;
                break;
            }
        }
    }

    if (ok) {
        /* 3. Semaphore: wait until ALL chunks are sorted. */
        if (sync_wait_all(&sync, workers) != 0)
            ok = 0;
    }

    if (ok) {
        /* 4. Read every child's "done" message. */
        for (int i = 0; i < workers; i++) {
            done_msg_t done;
            if (receive_done(&q, &done) != 0) {
                ok = 0;
                break;
            }
            if (!done.ok) {
                fprintf(stderr, "chunk %d (pid %d) failed\n", done.chunk_id, done.pid);
                ok = 0;
            } else if (verbose) {
                printf("  chunk %2d sorted by pid %-7d in %.3f s\n",
                       done.chunk_id, done.pid, done.seconds);
            }
        }
    } else {
        /* Something failed: children may be stuck waiting, stop them. */
        for (int i = 0; i < started; i++)
            kill(pids[i], SIGTERM);
    }

    /* 5. waitpid() every child we started, so no zombies are left. */
    for (int i = 0; i < started; i++) {
        int status;
        pid_t r;
        do {
            r = waitpid(pids[i], &status, 0);
        } while (r == -1 && errno == EINTR);   /* interrupted: try again */

        if (r == -1) {
            perror("waitpid");
            ok = 0;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            ok = 0;   /* child crashed or reported an error */
        }
    }

    /* 6. Double-check the shared counter the children updated. */
    if (ok && (progress->chunks_done != workers || progress->elements_sorted != n)) {
        fprintf(stderr, "progress mismatch: %d chunks, %zu elements\n",
                progress->chunks_done, progress->elements_sorted);
        ok = 0;
    }

    /* 7. Clean up all IPC objects. */
    sync_remove(&sync);
    queues_remove(&q);
    munmap(progress, sizeof(progress_t));
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* CSV output                                                         */
/* ------------------------------------------------------------------ */

/* Append one result line to the CSV file (header added if file is new). */
static void append_csv(const char *path, const char *mode, int workers, int run,
                       size_t n, double sort_s, double merge_s, int pass)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        perror(path);
        return;
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size == 0)
        dprintf(fd, "mode,workers,run,elements,sort_seconds,merge_seconds,total_seconds,result\n");
    dprintf(fd, "%s,%d,%d,%zu,%.6f,%.6f,%.6f,%s\n", mode, workers, run, n,
            sort_s, merge_s, sort_s + merge_s, pass ? "PASS" : "FAIL");
    close(fd);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-m thread|process] [-w workers 1..%d] [-n elements]\n"
            "          [-s seed] [-r run_number] [-c results.csv] [-q]\n",
            prog, MAX_WORKERS);
}

int main(int argc, char **argv)
{
    const char *mode = "process";
    int workers = 4;
    size_t n = 10000000;
    unsigned seed = 1;
    int run = 1;
    const char *csv = NULL;
    int verbose = 1;
    int opt;

    while ((opt = getopt(argc, argv, "m:w:n:s:r:c:qh")) != -1) {
        switch (opt) {
        case 'm': mode = optarg; break;
        case 'w': workers = atoi(optarg); break;
        case 'n': n = strtoull(optarg, NULL, 10); break;
        case 's': seed = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'r': run = atoi(optarg); break;
        case 'c': csv = optarg; break;
        case 'q': verbose = 0; break;
        default:  usage(argv[0]); return 1;
        }
    }
    int use_threads = strcmp(mode, "thread") == 0;
    if ((!use_threads && strcmp(mode, "process") != 0) ||
        workers < 1 || workers > MAX_WORKERS || n == 0) {
        usage(argv[0]);
        return 1;
    }

    /* Step 1: data in shared memory. */
    size_t bytes = n * sizeof(int32_t);
    int32_t *data = shared_alloc(bytes);
    int32_t *sorted = malloc(bytes);
    if (data == NULL || sorted == NULL) {
        perror("allocating data");
        return 1;
    }
    fill_random(data, n, seed);
    int64_t sum_before = checksum(data, n);

    printf("mode=%s workers=%d elements=%zu\n", mode, workers, n);

    /* Step 2: split into chunks. */
    chunk_t chunks[MAX_WORKERS];
    split_into_chunks(n, workers, chunks);

    /* Step 3: parallel sort. */
    double t0 = now_seconds();
    int rc = use_threads ? sort_with_threads(data, chunks, workers)
                         : sort_with_processes(data, chunks, workers, n, verbose);
    double t1 = now_seconds();

    /* Step 4: N-way merge (same code for both modes). */
    if (rc == 0)
        rc = merge_chunks(data, chunks, workers, sorted);
    double t2 = now_seconds();

    /* Step 5: verify (NOT timed). */
    int pass = (rc == 0) && is_sorted(sorted, n) && checksum(sorted, n) == sum_before;

    printf("sort  phase: %.3f s\n", t1 - t0);
    printf("merge phase: %.3f s\n", t2 - t1);
    printf("total      : %.3f s\n", t2 - t0);
    printf("%s\n", pass ? "PASS" : "FAIL");

    if (csv != NULL)
        append_csv(csv, mode, workers, run, n, t1 - t0, t2 - t1, pass);

    free(sorted);
    munmap(data, bytes);
    return pass ? 0 : 1;
}
