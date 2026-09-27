/*
 * merge.c - N-way merge with a min-heap.
 *
 * Idea: every chunk is sorted, so the smallest value that is left overall
 * must be at the FRONT of one of the chunks. We keep the front value of
 * every chunk in a min-heap (smallest value on top). Then we repeat:
 *
 *   1. take the top of the heap -> it is the next output value
 *   2. move to the next value of that same chunk and put it in the heap
 *      (if that chunk is empty, the heap just gets smaller)
 *
 * The heap holds at most k items, so each step costs O(log k).
 * Total cost: O(n log k) time, O(k) extra memory.
 */
#include <stdio.h>
#include <stdlib.h>

#include "merge.h"

typedef struct {
    int32_t value;   /* front value of a chunk */
    int     chunk;   /* which chunk it came from */
} heap_item_t;

/*
 * Move heap[i] down until both children are bigger than it.
 * The heap is stored in an array: the children of i are 2i+1 and 2i+2.
 */
static void sift_down(heap_item_t *heap, int size, int i)
{
    for (;;) {
        int left  = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < size && heap[left].value < heap[smallest].value)
            smallest = left;
        if (right < size && heap[right].value < heap[smallest].value)
            smallest = right;
        if (smallest == i)
            return;

        heap_item_t tmp = heap[i];
        heap[i] = heap[smallest];
        heap[smallest] = tmp;
        i = smallest;
    }
}

int merge_chunks(const int32_t *src, const chunk_t *chunks, int k, int32_t *dst)
{
    if (k <= 0)
        return -1;

    heap_item_t *heap = malloc((size_t)k * sizeof(heap_item_t));
    size_t *next = malloc((size_t)k * sizeof(size_t));  /* next index per chunk */
    if (heap == NULL || next == NULL) {
        perror("merge: malloc");
        free(heap);
        free(next);
        return -1;
    }

    /* Put the first value of every non-empty chunk into the heap. */
    int size = 0;
    for (int c = 0; c < k; c++) {
        next[c] = chunks[c].start;
        if (next[c] < chunks[c].end) {
            heap[size].value = src[next[c]];
            heap[size].chunk = c;
            size++;
        }
    }
    for (int i = size / 2 - 1; i >= 0; i--)   /* turn the array into a heap */
        sift_down(heap, size, i);

    /* Main loop: always output the smallest front value. */
    size_t out = 0;
    while (size > 0) {
        int c = heap[0].chunk;
        dst[out++] = heap[0].value;

        next[c]++;
        if (next[c] < chunks[c].end) {
            heap[0].value = src[next[c]];   /* same chunk, next value */
        } else {
            heap[0] = heap[size - 1];       /* chunk finished: remove it */
            size--;
        }
        sift_down(heap, size, 0);
    }

    free(heap);
    free(next);
    return 0;
}
