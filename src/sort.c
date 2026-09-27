/*
 * sort.c - Chunk splitting, merge sort for one chunk, and checking helpers.
 */
#include <stdlib.h>
#include <string.h>

#include "common.h"

void split_into_chunks(size_t n, int k, chunk_t *chunks)
{
    size_t size  = n / (size_t)k;   /* every chunk gets this many...       */
    size_t extra = n % (size_t)k;   /* ...and the first `extra` get 1 more */
    size_t pos = 0;

    for (int i = 0; i < k; i++) {
        size_t len = size + ((size_t)i < extra ? 1 : 0);
        chunks[i].start = pos;
        chunks[i].end   = pos + len;
        pos += len;
    }
}

/* Very small ranges are faster with insertion sort than with recursion. */
#define SMALL_RANGE 32

static void insertion_sort(int32_t *a, size_t lo, size_t hi)
{
    for (size_t i = lo + 1; i < hi; i++) {
        int32_t value = a[i];
        size_t j = i;
        while (j > lo && a[j - 1] > value) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = value;
    }
}

/* Merge the two sorted halves a[lo..mid) and a[mid..hi) using tmp. */
static void merge_halves(int32_t *a, int32_t *tmp, size_t lo, size_t mid, size_t hi)
{
    size_t i = lo, j = mid, k = lo;

    while (i < mid && j < hi)
        tmp[k++] = (a[i] <= a[j]) ? a[i++] : a[j++];
    while (i < mid)
        tmp[k++] = a[i++];
    while (j < hi)
        tmp[k++] = a[j++];

    memcpy(a + lo, tmp + lo, (hi - lo) * sizeof(int32_t));
}

/* Classic top-down merge sort: sort left half, sort right half, merge. */
static void sort_range(int32_t *a, int32_t *tmp, size_t lo, size_t hi)
{
    if (hi - lo <= SMALL_RANGE) {
        insertion_sort(a, lo, hi);
        return;
    }
    size_t mid = lo + (hi - lo) / 2;
    sort_range(a, tmp, lo, mid);
    sort_range(a, tmp, mid, hi);
    merge_halves(a, tmp, lo, mid, hi);
}

int merge_sort(int32_t *a, size_t n)
{
    if (n < 2)
        return 0;

    /* One temporary buffer for the whole sort, allocated once. */
    int32_t *tmp = malloc(n * sizeof(int32_t));
    if (tmp == NULL)
        return -1;

    sort_range(a, tmp, 0, n);
    free(tmp);
    return 0;
}

int is_sorted(const int32_t *a, size_t n)
{
    for (size_t i = 1; i < n; i++)
        if (a[i - 1] > a[i])
            return 0;
    return 1;
}

int64_t checksum(const int32_t *a, size_t n)
{
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += a[i];
    return sum;
}
