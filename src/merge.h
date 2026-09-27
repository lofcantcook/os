/*
 * merge.h - N-way merge of sorted chunks using a binary min-heap.
 *
 * Cost: O(n log k) comparisons for n total elements and k chunks.
 */
#ifndef MERGE_H
#define MERGE_H

#include <stddef.h>
#include <stdint.h>

#include "common.h"

/*
 * Merge k sorted chunks of `src` into `dst` (which must hold the sum of all
 * chunk lengths and must not overlap `src`). Returns 0, or -1 on bad input.
 */
int merge_k(const int32_t *src, const chunk_t *chunks, int k, int32_t *dst);

/*
 * Same merge, but streams the result to file descriptor `fd` through a
 * buffer of `buf_bytes` (e.g. 8 MiB) instead of needing a second full-size
 * array. Handles partial writes and EINTR. Returns 0, or -1 on error.
 */
int merge_k_to_fd(const int32_t *src, const chunk_t *chunks, int k,
                  int fd, size_t buf_bytes);

#endif /* MERGE_H */
