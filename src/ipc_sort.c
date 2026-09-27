/*
 * ipc_sort.c - Multiprocess sort driver that wires the IPC pieces together.
 *
 *   1. Data lives in a MAP_SHARED mapping visible to every worker.
 *   2. The parent dispatches chunk boundaries over a POSIX message queue.
 *   3. Workers sort their chunk, update a process-shared mutex-protected
 *      counter, sem_post() the "sorted" semaphore and report status back
 *      over a second message queue.
 *   4. The parent sem_wait()s for all chunks, checks every status, tells
 *      workers to exit, reaps them with waitpid(), then runs the N-way merge.
 *
 * The anonymous shared mapping here can be swapped for the shm_open()-based
 * segment from the shared-memory module without changing the IPC logic.
 *
 * Usage: ipc_sort [-n elements] [-w workers] [-s seed] [-o output_file] [-q]
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "ipc_sync.h"
#include "merge.h"
#include "msgq.h"

#define DEFAULT_N       10000000UL
#define DEFAULT_WORKERS 4
#define MERGE_BUF_BYTES (8UL << 20)

typedef struct {
    pid_t pids[MAX_WORKERS];
    int   n;
} worker_set_t;

/* ---- worker ---------------------------------------------------------- */

static int worker_main(int32_t *data, msgq_t *q, ipc_sync_t *sy)
{
    for (;;) {
        task_msg_t t;
        status_msg_t st;
        double t0;

        if (msgq_recv_task(q, &t) != 0)
            return 1;
        if (t.type == TASK_EXIT)
            return 0;
        if (t.type != TASK_SORT) {
            fprintf(stderr, "worker %d: unknown task type %d\n", (int)getpid(), t.type);
            return 1;
        }

        t0 = now_sec();
        sort_chunk(data + t.offset, (size_t)t.len);

        memset(&st, 0, sizeof(st));
        st.chunk_id = t.chunk_id;
        st.pid = getpid();
        st.sort_sec = now_sec() - t0;
        st.status = is_sorted(data + t.offset, (size_t)t.len) ? 0 : EINVAL;

        /* Signal first, then report: the parent passes the semaphore gate
         * before draining statuses, so a full status queue cannot block the
         * post and deadlock the merge phase. */
        if (ipc_sync_chunk_done(sy, (size_t)t.len, st.status != 0) != 0)
            return 1;
        if (msgq_send_status(q, &st) != 0)
            return 1;
    }
}

/* ---- parent helpers -------------------------------------------------- */

/* Called while waiting on the semaphore: detect a worker that died early. */
static int any_worker_died(void *arg)
{
    worker_set_t *ws = arg;
    int status;

    for (int i = 0; i < ws->n; i++) {
        if (ws->pids[i] <= 0)
            continue;
        pid_t r = waitpid(ws->pids[i], &status, WNOHANG);
        if (r == ws->pids[i]) {
            fprintf(stderr, "worker %d exited before finishing (status 0x%x)\n",
                    (int)r, status);
            ws->pids[i] = 0;
            return 1;
        }
    }
    return 0;
}

/* Reap every worker so none is left as a zombie. Returns #abnormal exits. */
static int reap_workers(worker_set_t *ws)
{
    int bad = 0;

    for (int i = 0; i < ws->n; i++) {
        int status;

        if (ws->pids[i] <= 0)
            continue;
        while (waitpid(ws->pids[i], &status, 0) == -1) {
            if (errno != EINTR) {
                perror("waitpid");
                bad++;
                goto next;
            }
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            bad++;
next:
        ws->pids[i] = 0;
    }
    return bad;
}

static void kill_workers(worker_set_t *ws)
{
    for (int i = 0; i < ws->n; i++)
        if (ws->pids[i] > 0)
            kill(ws->pids[i], SIGTERM);
}

static void fill_random(int32_t *a, size_t n, uint64_t seed)
{
    uint64_t x = seed ? seed : 0x2545F4914F6CDD1DULL;

    for (size_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        a[i] = (int32_t)(uint32_t)(x >> 32);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-n elements] [-w workers 1..%d] [-s seed] [-o output_file] [-q]\n",
            prog, MAX_WORKERS);
}

/* ---- main ------------------------------------------------------------ */

int main(int argc, char **argv)
{
    size_t n = DEFAULT_N;
    int workers = DEFAULT_WORKERS;
    uint64_t seed = 42;
    const char *out_path = NULL;
    int quiet = 0;
    int opt, rc = 1;

    while ((opt = getopt(argc, argv, "n:w:s:o:qh")) != -1) {
        switch (opt) {
        case 'n': n = strtoull(optarg, NULL, 10); break;
        case 'w': workers = atoi(optarg); break;
        case 's': seed = strtoull(optarg, NULL, 10); break;
        case 'o': out_path = optarg; break;
        case 'q': quiet = 1; break;
        default:  usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }
    if (n == 0 || workers < 1 || workers > MAX_WORKERS) {
        usage(argv[0]);
        return 1;
    }

    size_t data_bytes = n * sizeof(int32_t);
    int32_t *data = MAP_FAILED, *sorted = NULL;
    void *sync_mem = MAP_FAILED;
    msgq_t q;
    ipc_sync_t sy;
    int have_q = 0, have_sync = 0;
    worker_set_t ws = { .n = 0 };
    chunk_t chunks[MAX_WORKERS];
    double t_start, t_sorted, t_merged;

    memset(&q, 0, sizeof(q));
    memset(&sy, 0, sizeof(sy));

    data = mmap(NULL, data_bytes, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    sync_mem = mmap(NULL, sizeof(shared_progress_t), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED || sync_mem == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }

    fill_random(data, n, seed);
    uint64_t sum_before = checksum(data, n);

    if (msgq_create(&q) != 0)
        goto cleanup;
    have_q = 1;
    if (ipc_sync_create(&sy, sync_mem) != 0)
        goto cleanup;
    have_sync = 1;

    chunks_split(n, workers, chunks);
    t_start = now_sec();

    /* Fork the worker pool. */
    for (int i = 0; i < workers; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            kill_workers(&ws);
            reap_workers(&ws);
            goto cleanup;
        }
        if (pid == 0) {
            int wrc = worker_main(data, &q, &sy);
            msgq_close(&q);
            ipc_sync_close(&sy);
            _exit(wrc);
        }
        ws.pids[ws.n++] = pid;
    }

    /* Dispatch chunk boundaries. */
    for (int i = 0; i < workers; i++) {
        task_msg_t t = { .type = TASK_SORT, .chunk_id = i,
                         .offset = chunks[i].offset, .len = chunks[i].len };
        if (msgq_send_task(&q, &t) != 0) {
            kill_workers(&ws);
            reap_workers(&ws);
            goto cleanup;
        }
    }

    /* Merge gate: wait until every chunk has been sorted. */
    if (ipc_sync_wait_all(&sy, workers, 1, any_worker_died, &ws) != 0) {
        kill_workers(&ws);
        reap_workers(&ws);
        goto cleanup;
    }

    /* Collect per-chunk status reports. */
    int failed = 0;
    for (int i = 0; i < workers; i++) {
        status_msg_t st;

        if (msgq_recv_status(&q, &st) != 0) {
            failed = 1;
            break;
        }
        if (st.chunk_id < 0 || st.chunk_id >= workers) {
            fprintf(stderr, "bad chunk id %d in status report\n", st.chunk_id);
            failed = 1;
        } else if (st.status != 0) {
            fprintf(stderr, "chunk %d (pid %d) failed: %s\n",
                    st.chunk_id, (int)st.pid, strerror(st.status));
            failed = 1;
        } else if (!quiet) {
            printf("  chunk %2d  pid %-7d  %10llu elems  sorted in %.3f s\n",
                   st.chunk_id, (int)st.pid,
                   (unsigned long long)chunks[st.chunk_id].len, st.sort_sec);
        }
    }

    /* Tell every worker to stop, then reap them (no zombies). */
    for (int i = 0; i < workers; i++) {
        task_msg_t t = { .type = TASK_EXIT };
        if (msgq_send_task(&q, &t) != 0) {
            kill_workers(&ws);
            break;
        }
    }
    if (reap_workers(&ws) != 0) {
        fprintf(stderr, "one or more workers exited abnormally\n");
        failed = 1;
    }
    if (failed || sy.progress->failures != 0 || sy.progress->elems_sorted != n) {
        fprintf(stderr, "sort phase failed (done=%zu, elems=%zu, failures=%d)\n",
                sy.progress->chunks_done, sy.progress->elems_sorted,
                sy.progress->failures);
        goto cleanup;
    }
    t_sorted = now_sec();

    /* N-way merge. */
    if (out_path) {
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(out_path);
            goto cleanup;
        }
        int mrc = merge_k_to_fd(data, chunks, workers, fd, MERGE_BUF_BYTES);
        if (close(fd) != 0) {
            perror("close");
            mrc = -1;
        }
        if (mrc != 0)
            goto cleanup;
        t_merged = now_sec();
        printf("merged output written to %s\n", out_path);
    } else {
        sorted = malloc(data_bytes);
        if (!sorted) {
            perror("malloc");
            goto cleanup;
        }
        if (merge_k(data, chunks, workers, sorted) != 0)
            goto cleanup;
        t_merged = now_sec();

        if (!is_sorted(sorted, n) || checksum(sorted, n) != sum_before) {
            fprintf(stderr, "VERIFY FAILED: output is not a sorted permutation\n");
            goto cleanup;
        }
        printf("verify: OK (sorted, checksum matches)\n");
    }

    printf("workers=%d n=%zu sort=%.3f s merge=%.3f s total=%.3f s\n",
           workers, n, t_sorted - t_start, t_merged - t_sorted, t_merged - t_start);
    rc = 0;

cleanup:
    free(sorted);
    if (have_sync)
        ipc_sync_destroy(&sy);
    if (have_q)
        msgq_destroy(&q);
    if (sync_mem != MAP_FAILED)
        munmap(sync_mem, sizeof(shared_progress_t));
    if (data != MAP_FAILED)
        munmap(data, data_bytes);
    return rc;
}
