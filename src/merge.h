/*
 * merge.h - N-way merge of sorted chunks using a min-heap.
 * Used by BOTH the thread mode and the process mode.
 */
#ifndef MERGE_H
#define MERGE_H

#include <stdint.h>

#include "common.h"

/*
 * `src` contains k chunks, each already sorted on its own.
 * Writes all elements, fully sorted, into `dst` (a separate array that is
 * big enough for all of them). Returns 0 on success, -1 on error.
 */
int merge_chunks(const int32_t *src, const chunk_t *chunks, int k, int32_t *dst);

#endif
