/*
 * merge.c - N-way merge with a min-heap keyed on each chunk's head element.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "merge.h"

typedef struct {
    int32_t val;  /* current head value of this chunk */
    int     src;  /* which chunk it came from */
} heap_node_t;

typedef struct {
    heap_node_t *node;
    int size;
} min_heap_t;

/* Ties are broken by chunk index so the merge is stable across chunks. */
static inline int node_less(const heap_node_t *a, const heap_node_t *b)
{
    return a->val < b->val || (a->val == b->val && a->src < b->src);
}

static void sift_down(min_heap_t *h, int i)
{
    heap_node_t tmp = h->node[i];

    for (;;) {
        int l = 2 * i + 1;
        int r = l + 1;
        int m = l;

        if (l >= h->size)
            break;
        if (r < h->size && node_less(&h->node[r], &h->node[l]))
            m = r;
        if (!node_less(&h->node[m], &tmp))
            break;
        h->node[i] = h->node[m];
        i = m;
    }
    h->node[i] = tmp;
}

/*
 * Shared merge loop. Output goes either into `dst` directly or, when dst is
 * NULL, into `buf` which `flush` drains whenever it fills up.
 */
typedef int (*flush_fn)(const int32_t *buf, size_t n, void *ctx);

static int merge_core(const int32_t *src, const chunk_t *chunks, int k,
                      int32_t *out, size_t out_cap, flush_fn flush, void *ctx)
{
    min_heap_t h;
    size_t *pos, *end;
    size_t o = 0;
    int rc = -1;

    if (k <= 0 || !src || !chunks || !out)
        return -1;

    h.node = malloc((size_t)k * sizeof(*h.node));
    pos = malloc((size_t)k * sizeof(*pos));
    end = malloc((size_t)k * sizeof(*end));
    if (!h.node || !pos || !end) {
        perror("merge: malloc");
        goto out;
    }

    h.size = 0;
    for (int i = 0; i < k; i++) {
        pos[i] = chunks[i].offset;
        end[i] = chunks[i].offset + chunks[i].len;
        if (pos[i] < end[i]) {
            h.node[h.size].val = src[pos[i]];
            h.node[h.size].src = i;
            h.size++;
        }
    }
    for (int i = h.size / 2 - 1; i >= 0; i--)
        sift_down(&h, i);

    while (h.size > 0) {
        int s = h.node[0].src;

        out[o++] = h.node[0].val;
        if (flush && o == out_cap) {
            if (flush(out, o, ctx) != 0)
                goto out;
            o = 0;
        }

        if (++pos[s] < end[s]) {
            h.node[0].val = src[pos[s]];   /* replace root with next element */
        } else {
            h.node[0] = h.node[--h.size];  /* chunk exhausted, drop it */
        }
        if (h.size > 0)
            sift_down(&h, 0);
    }

    if (flush && o > 0 && flush(out, o, ctx) != 0)
        goto out;
    rc = 0;

out:
    free(h.node);
    free(pos);
    free(end);
    return rc;
}

int merge_k(const int32_t *src, const chunk_t *chunks, int k, int32_t *dst)
{
    return merge_core(src, chunks, k, dst, 0, NULL, NULL);
}

/* write() loop that handles short writes and EINTR. */
static int write_all_fn(const int32_t *buf, size_t n, void *ctx)
{
    int fd = *(int *)ctx;
    const char *p = (const char *)buf;
    size_t left = n * sizeof(int32_t);

    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            perror("merge: write");
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }
    return 0;
}

int merge_k_to_fd(const int32_t *src, const chunk_t *chunks, int k,
                  int fd, size_t buf_bytes)
{
    size_t cap = buf_bytes / sizeof(int32_t);
    int32_t *buf;
    int rc;

    if (cap == 0)
        cap = 1;
    buf = malloc(cap * sizeof(int32_t));
    if (!buf) {
        perror("merge: malloc buffer");
        return -1;
    }
    rc = merge_core(src, chunks, k, buf, cap, write_all_fn, &fd);
    free(buf);
    return rc;
}
