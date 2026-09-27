/*
 * chunk.c - Chunk splitting, per-chunk sort and verification helpers.
 */
#include <stdlib.h>

#include "common.h"

void chunks_split(size_t n, int k, chunk_t *chunks)
{
    size_t base = n / (size_t)k;
    size_t extra = n % (size_t)k;
    size_t off = 0;

    for (int i = 0; i < k; i++) {
        size_t len = base + ((size_t)i < extra ? 1 : 0);
        chunks[i].offset = off;
        chunks[i].len = len;
        off += len;
    }
}

static int cmp_int32(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a;
    int32_t y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

void sort_chunk(int32_t *base, size_t n)
{
    if (n > 1)
        qsort(base, n, sizeof(int32_t), cmp_int32);
}

uint64_t checksum(const int32_t *a, size_t n)
{
    uint64_t sum = 0, xr = 0;

    for (size_t i = 0; i < n; i++) {
        uint32_t v = (uint32_t)a[i];
        sum += v;
        xr ^= (uint64_t)v * 0x9E3779B97F4A7C15ULL;
    }
    return sum ^ (xr << 1);
}

int is_sorted(const int32_t *a, size_t n)
{
    for (size_t i = 1; i < n; i++)
        if (a[i - 1] > a[i])
            return 0;
    return 1;
}
