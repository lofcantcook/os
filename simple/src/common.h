/*
 * common.h - Small shared definitions used by every file.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MAX_WORKERS 64

/*
 * One piece of the big array. It covers the elements
 * data[start], data[start + 1], ..., data[end - 1]   (end is NOT included).
 */
typedef struct {
    size_t start;
    size_t end;
} chunk_t;

/* Current time in seconds from a clock that never jumps backwards. */
static inline double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Split n elements into k chunks of (almost) the same size. */
void split_into_chunks(size_t n, int k, chunk_t *chunks);

/* Sort a[0..n) with merge sort. Returns 0 on success, -1 if out of memory. */
int merge_sort(int32_t *a, size_t n);

/* Returns 1 if a[0..n) is in ascending order, otherwise 0. */
int is_sorted(const int32_t *a, size_t n);

/*
 * Sum of all values. Sorting only moves values around, so the sum must be
 * the same before and after. (1 billion * 2^31 still fits in 64 bits.)
 */
int64_t checksum(const int32_t *a, size_t n);

#endif
