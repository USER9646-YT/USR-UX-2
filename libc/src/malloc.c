#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include "internal.h"

// Every block (free or used) sits in one address-ordered doubly linked list.
struct hdr {
    size_t        size;                 // payload bytes, a multiple of 16
    struct hdr   *prev;
    struct hdr   *next;
    unsigned int  magic;
};

#define HDR     sizeof(struct hdr)      // 16 bytes
#define M_USED  0xA110CA7Eu
#define M_FREE  0xF4EEF4EEu
#define ALIGN16(x) (((x) + 15u) & ~15u)
#define GROW    65536u

static struct hdr *first, *last, *rover;

static struct hdr *grow(size_t need) {
    size_t total = ALIGN16(need + HDR);
    if (total < GROW) total = GROW;
    total = (total + 4095u) & ~4095u;

    void *p = sbrk((ptrdiff_t)total);
    if (p == (void *)-1) return NULL;

    struct hdr *h = p;
    h->size  = total - HDR;
    h->magic = M_FREE;
    h->prev  = last;
    h->next  = NULL;
    if (last) last->next = h; else first = h;
    last = h;

    struct hdr *pr = h->prev;                     // merge with a free neighbour just before
    if (pr && pr->magic == M_FREE && (char *)pr + HDR + pr->size == (char *)h) {
        pr->size += HDR + h->size;
        pr->next = NULL;
        last = pr;
        return pr;
    }
    return h;
}

static struct hdr *find(size_t need) {
    for (struct hdr *h = rover; h; h = h->next)
        if (h->magic == M_FREE && h->size >= need) return h;
    for (struct hdr *h = first; h && h != rover; h = h->next)
        if (h->magic == M_FREE && h->size >= need) return h;
    return NULL;
}

static void split(struct hdr *h, size_t need) {
    if (h->size < need + HDR + 16) return;

    struct hdr *r = (struct hdr *)((char *)h + HDR + need);
    r->size  = h->size - need - HDR;
    r->magic = M_FREE;
    r->prev  = h;
    r->next  = h->next;
    if (h->next) h->next->prev = r; else last = r;
    h->next = r;
    h->size = need;
}

void *malloc(size_t n) {
    if (n == 0) n = 1;
    if (n > 0x7FFFFF00u) { errno = ENOMEM; return NULL; }

    size_t need = ALIGN16(n);
    struct hdr *h = find(need);
    if (!h) {
        h = grow(need);
        if (!h || h->size < need) { errno = ENOMEM; return NULL; }
    }

    split(h, need);
    h->magic = M_USED;
    rover = h->next;
    return (char *)h + HDR;
}

void free(void *p) {
    if (!p) return;

    struct hdr *h = (struct hdr *)((char *)p - HDR);
    if (h->magic != M_USED) {
        write(2, "free(): invalid pointer\n", 24);
        abort();
    }
    h->magic = M_FREE;

    struct hdr *n = h->next;
    if (n && n->magic == M_FREE && (char *)h + HDR + h->size == (char *)n) {
        h->size += HDR + n->size;
        h->next = n->next;
        if (n->next) n->next->prev = h; else last = h;
        if (rover == n) rover = h;
    }

    struct hdr *pr = h->prev;
    if (pr && pr->magic == M_FREE && (char *)pr + HDR + pr->size == (char *)h) {
        pr->size += HDR + h->size;
        pr->next = h->next;
        if (h->next) h->next->prev = pr; else last = pr;
        if (rover == h) rover = pr;
    }
}

void *calloc(size_t n, size_t size) {
    if (size && n > 0x7FFFFF00u / size) { errno = ENOMEM; return NULL; }
    void *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return NULL; }

    struct hdr *h = (struct hdr *)((char *)p - HDR);
    if (h->magic != M_USED) {
        write(2, "realloc(): invalid pointer\n", 27);
        abort();
    }

    size_t need = ALIGN16(n);
    if (need <= h->size) {                        // shrink in place
        split(h, need);
        return p;
    }

    struct hdr *nx = h->next;                     // grow into a free neighbour
    if (nx && nx->magic == M_FREE && (char *)h + HDR + h->size == (char *)nx &&
        h->size + HDR + nx->size >= need) {
        h->size += HDR + nx->size;
        h->next = nx->next;
        if (nx->next) nx->next->prev = h; else last = h;
        if (rover == nx) rover = h;
        split(h, need);
        return p;
    }

    void *q = malloc(n);
    if (!q) return NULL;
    memcpy(q, p, h->size < n ? h->size : n);
    free(p);
    return q;
}