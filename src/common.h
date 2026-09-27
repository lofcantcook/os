/*
 * common.h - Shared types and helpers used by every module.
 *
 * Agreed interface between the I/O, threading, multiprocessing and
 * IPC/merge parts of the project. Keep this file small and stable.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MAX_WORKERS 64

/* One contiguous slice of the data array, in elements (not bytes). */
typedef struct {
    size_t offset;
    size_t len;
} chunk_t;

/* Monotonic wall-clock time in seconds. */
static inline double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Split n elements into k nearly equal chunks (first n % k get one extra). */
void chunks_split(size_t n, int k, chunk_t *chunks);

/* Sort base[0..n) ascending, in place. */
void sort_chunk(int32_t *base, size_t n);

/* Order-independent checksum used to verify nothing was lost or duplicated. */
uint64_t checksum(const int32_t *a, size_t n);

/* Returns 1 if a[0..n) is non-decreasing, 0 otherwise. */
int is_sorted(const int32_t *a, size_t n);

#endif /* COMMON_H */
