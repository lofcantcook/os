# Parallel Merge Sort (simple version)

Sorts a big array of `int32_t` values in parallel, with **threads** or
**processes**, and then merges the sorted pieces with a **min-heap N-way merge**.
This folder covers message passing, synchronization, the merge, the Makefile
and integration. The code keeps each step short and commented.

## Required libraries

Linux with GCC and make. Everything used is in glibc (pthreads, POSIX message
queues, POSIX semaphores, `mmap`). `-lrt` is linked for older systems.

```sh
sudo apt install build-essential        # Ubuntu / Debian
sudo pacman -S --needed base-devel      # Arch / Manjaro
```

## Build & run

```sh
make                 # builds ./parallel_merge_sort
make run             # 10 million numbers, 4 threads, then 4 processes
make test            # merge unit tests + small runs (prints PASS)
make bench           # 1..10 workers x 3 runs x both modes -> raw_data.csv
make bench BENCH_N=10000000   # the same, but with a smaller dataset
make clean
```

Run it yourself:

```sh
./parallel_merge_sort -m process -w 8 -n 1000000000
./parallel_merge_sort -m thread  -w 8 -n 1000000000 -c raw_data.csv -r 1
```

| Option | Meaning | Default |
|---|---|---|
| `-m thread\|process` | how to run the workers | `process` |
| `-w N` | number of workers (1–64) | 4 |
| `-n N` | number of elements | 10 000 000 |
| `-s N` | random seed | 1 |
| `-r N` | run number (written to the CSV) | 1 |
| `-c file` | append a result line to this CSV file | off |
| `-q` | quiet (no per-chunk lines) | off |

Output looks like this:

```
mode=process workers=4 elements=10000000
  chunk  2 sorted by pid 780     in 0.232 s
  ...
sort  phase: 0.262 s
merge phase: 0.227 s
total      : 0.489 s
PASS
```

**Memory:** the program needs about 2× the data size (the shared data array plus
the merge output array), so 1 billion numbers need about 8 GB of RAM.

## Files

| File | What it does |
|---|---|
| `src/common.h` | `chunk_t` (start/end index), timer, helper declarations |
| `src/sort.c` | split into chunks, merge sort for one chunk, `is_sorted`, checksum |
| `src/message_queue.h/.c` | POSIX message queues: parent sends tasks, children send "done" |
| `src/sync.h/.c` | named semaphore (merge gate) + process-shared mutex (progress counter) |
| `src/merge.h/.c` | N-way merge with a min-heap, used by both modes |
| `src/main.c` | puts everything together: thread mode, process mode, timing, PASS/FAIL, CSV |
| `tests/test_merge.c` | small tests for the merge |

## How the process mode works

```
parent                                  child (one per chunk)
------                                  ---------------------
mq_open  task queue + done queue
sem_open "all sorted" (value 0)
mutex in shared memory (PROCESS_SHARED)
fork() N times  ─────────────────────►  mq_receive(task)       waits for work
mq_send(task) N times ───────────────►  merge_sort(data[start..end))
   {chunk_id, start, end}               lock mutex, counter++, unlock
sem_wait() N times   ◄────────────────  sem_post()
mq_receive(done) N times ◄────────────  mq_send(done) {chunk, pid, ok, time}
waitpid() every child (no zombies)      _exit()
check the shared counter
merge_chunks()   (min-heap N-way merge)
verify -> PASS / FAIL
mq_close/mq_unlink, sem_close/sem_unlink, pthread_mutex_destroy
```

### Message passing (`message_queue.c`)
* **Task queue** (parent → children): `{chunk_id, start, end}`, the chunk boundaries.
* **Done queue** (children → parent): `{chunk_id, pid, ok, seconds}`, a completion report.
* Queue names contain the parent's PID, so two runs never clash. They are
  removed with `mq_unlink` at the end.
* If a signal interrupts a send or receive (`EINTR`), it is simply retried.
* The queue depth is 10, the Linux limit for normal users. With more than 10
  workers `mq_send` just waits until a child takes a message, so it still works.

### Synchronization (`sync.c`)
* **Semaphore = merge gate.** It starts at 0. Each child calls `sem_post` once;
  the parent calls `sem_wait` N times. The merge cannot start before every
  chunk is sorted.
* **Mutex = protects shared state.** A `progress_t` counter (chunks done,
  elements sorted) lives in `MAP_SHARED` memory and every child updates it.
  The mutex has `PTHREAD_PROCESS_SHARED` so it works between processes. The
  parent checks afterwards that `elements_sorted == n`.
* **No deadlock:** a child posts the semaphore *before* sending its "done"
  message. The parent reads the done queue only after the semaphore, so a
  full queue can never stop a child from posting.
* Only one lock is ever held at a time, so there is no lock-ordering problem.
* A child whose sort fails still posts (with `ok = 0`), so the parent never
  waits forever for it.

### Thread mode
N `pthread`s each merge-sort their own chunk in place, then wait at a
`pthread_barrier_t` (N + 1 members, the main thread included). After the barrier
the main thread joins the threads and runs the same `merge_chunks()`.

### N-way merge (`merge.c`)
Each chunk is sorted, so the smallest remaining value is always at the front of
one of the chunks. A min-heap holds the front value of every chunk:

1. take the top of the heap → it is the next output value;
2. replace it with the next value from the same chunk (or remove that chunk if
   it is empty) and sift down.

The heap has at most k items, so this costs **O(n log k)** time and **O(k)**
extra memory. The merge runs on a single thread, so it is the sequential part
of the program (the *s* in Amdahl's Law). An improvement would be a parallel
merge tree, where pairs of chunks are merged at the same time.

## Integration points
* **Shared memory:** `shared_alloc()` in `main.c` uses an anonymous
  `mmap(MAP_SHARED)`. It can be replaced by the `shm_open` + `ftruncate` +
  `mmap(MAP_SHARED)` segment (and `munmap` + `shm_unlink` at the end).
* **File I/O:** `fill_random()` in `main.c` can be replaced by writing the
  dataset with `write()` and loading it into shared memory with `read()`.
* **Timing:** the sort and merge phases are already timed with
  `clock_gettime(CLOCK_MONOTONIC)`. Verification is not included in the timing.
