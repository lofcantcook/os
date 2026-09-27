/*
 * test_merge.c - Quick checks for merge_sort() and merge_chunks().
 */
#include <stdio.h>
#include <stdlib.h>

#include "../src/common.h"
#include "../src/merge.h"

static int failures = 0;

/* Sort each chunk, merge them, and check the result. */
static void check(const char *name, int32_t *a, size_t n, const chunk_t *chunks, int k)
{
    int32_t *out = malloc((n > 0 ? n : 1) * sizeof(int32_t));
    int64_t sum = checksum(a, n);

    for (int i = 0; i < k; i++)
        merge_sort(a + chunks[i].start, chunks[i].end - chunks[i].start);

    int ok = merge_chunks(a, chunks, k, out) == 0 &&
             is_sorted(out, n) && checksum(out, n) == sum;

    printf("%-20s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
    free(out);
}

int main(void)
{
    /* One element. */
    int32_t a1[] = { 5 };
    chunk_t c1[] = { { 0, 1 } };
    check("single element", a1, 1, c1, 1);

    /* Some chunks are empty. */
    int32_t a2[] = { 3, 1, 2, 9, 7, 8 };
    chunk_t c2[] = { { 0, 3 }, { 3, 3 }, { 3, 6 }, { 6, 6 } };
    check("empty chunks", a2, 6, c2, 4);

    /* Every value the same. */
    int32_t a3[] = { 7, 7, 7, 7, 7, 7, 7 };
    chunk_t c3[] = { { 0, 2 }, { 2, 4 }, { 4, 7 } };
    check("all duplicates", a3, 7, c3, 3);

    /* Smallest and largest int32 values. */
    int32_t a4[] = { INT32_MAX, INT32_MIN, 0, -1, 1, INT32_MIN, INT32_MAX };
    chunk_t c4[] = { { 0, 3 }, { 3, 7 } };
    check("extreme values", a4, 7, c4, 2);

    /* Random data with 1 to 10 chunks. */
    size_t n = 1000003;
    int32_t *a = malloc(n * sizeof(int32_t));
    chunk_t chunks[MAX_WORKERS];
    for (int k = 1; k <= 10; k++) {
        char name[32];
        srand((unsigned)k);
        for (size_t i = 0; i < n; i++)
            a[i] = (int32_t)(((unsigned)rand() << 16) ^ (unsigned)rand());
        split_into_chunks(n, k, chunks);
        snprintf(name, sizeof(name), "random, %d chunks", k);
        check(name, a, n, chunks, k);
    }
    free(a);

    if (failures > 0) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
