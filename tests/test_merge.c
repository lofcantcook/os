/*
 * test_merge.c - Unit tests for the N-way merge (edge cases + random).
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/common.h"
#include "../src/merge.h"

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

static void run_case(const char *name, int32_t *a, size_t n, const chunk_t *c, int k)
{
    int32_t *out = malloc((n ? n : 1) * sizeof(int32_t));
    uint64_t sum = checksum(a, n);

    for (int i = 0; i < k; i++)
        sort_chunk(a + c[i].offset, c[i].len);

    CHECK(merge_k(a, c, k, out) == 0, name);
    CHECK(is_sorted(out, n), name);
    CHECK(checksum(out, n) == sum, name);
    free(out);
}

static void test_edge_cases(void)
{
    int32_t a1[] = { 5 };
    chunk_t c1[] = { { 0, 1 } };
    run_case("single element", a1, 1, c1, 1);

    int32_t a2[] = { 3, 1, 2, 9, 7, 8 };
    chunk_t c2[] = { { 0, 3 }, { 3, 0 }, { 3, 3 }, { 6, 0 } };
    run_case("empty chunks", a2, 6, c2, 4);

    int32_t a3[] = { 7, 7, 7, 7, 7, 7, 7 };
    chunk_t c3[] = { { 0, 2 }, { 2, 2 }, { 4, 3 } };
    run_case("all duplicates", a3, 7, c3, 3);

    int32_t a4[] = { INT32_MAX, INT32_MIN, 0, -1, 1, INT32_MIN, INT32_MAX };
    chunk_t c4[] = { { 0, 3 }, { 3, 4 } };
    run_case("extreme values", a4, 7, c4, 2);

    CHECK(merge_k(a1, c1, 0, a1 + 0) == -1, "k == 0 rejected");
}

static void test_random(void)
{
    size_t n = 1000003;
    int32_t *a = malloc(n * sizeof(int32_t));
    chunk_t c[MAX_WORKERS];

    for (int k = 1; k <= 16; k++) {
        char name[32];
        srand((unsigned)k);
        for (size_t i = 0; i < n; i++)
            a[i] = (int32_t)(((uint32_t)rand() << 16) ^ (uint32_t)rand());
        chunks_split(n, k, c);
        snprintf(name, sizeof(name), "random k=%d", k);
        run_case(name, a, n, c, k);
    }
    free(a);
}

static void test_to_fd(void)
{
    size_t n = 100000;
    int32_t *a = malloc(n * sizeof(int32_t));
    int32_t *back = malloc(n * sizeof(int32_t));
    chunk_t c[5];
    char path[] = "/tmp/test_merge_XXXXXX";
    int fd = mkstemp(path);

    CHECK(fd >= 0, "mkstemp");
    for (size_t i = 0; i < n; i++)
        a[i] = (int32_t)(rand() - RAND_MAX / 2);
    uint64_t sum = checksum(a, n);
    chunks_split(n, 5, c);
    for (int i = 0; i < 5; i++)
        sort_chunk(a + c[i].offset, c[i].len);

    /* Tiny 100-byte buffer forces many flushes. */
    CHECK(merge_k_to_fd(a, c, 5, fd, 100) == 0, "merge_k_to_fd");
    CHECK(lseek(fd, 0, SEEK_SET) == 0, "lseek");

    size_t got = 0;
    while (got < n * sizeof(int32_t)) {
        ssize_t r = read(fd, (char *)back + got, n * sizeof(int32_t) - got);
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    CHECK(got == n * sizeof(int32_t), "read back full size");
    CHECK(is_sorted(back, n), "fd output sorted");
    CHECK(checksum(back, n) == sum, "fd output checksum");

    close(fd);
    unlink(path);
    free(a);
    free(back);
}

int main(void)
{
    test_edge_cases();
    test_random();
    test_to_fd();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_merge: all tests passed\n");
    return 0;
}
