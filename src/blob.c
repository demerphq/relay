#include "relay.h"

// static unsigned long total_allocated = 0;
INLINE void *realloc_or_die(void *p, size_t size) {
   // _TD("realloc %zu allocated sofar %lu",size,total_allocated++);
    p = realloc(p,size);
    if (!p)
        SAYX(EXIT_FAILURE,"unable to allocate %zu bytes",size);
    return p;
}

INLINE void *malloc_or_die(size_t size) {
    return realloc_or_die(NULL,size);
}


INLINE blob_t * b_new(void) {
    blob_t *b;
    b = malloc_or_die(sizeof(*b));
    b->data = NULL;
    b->size = 0;
    b->pos = 0;
    b->next = NULL;
    return b;
}

INLINE blob_t * b_clone(blob_t *b) {
    blob_t *clone= b_new();
    clone->size= b->size;
    clone->pos= b->pos;
    clone->data = malloc_or_die(clone->size);
    memcpy(clone->data, b->data, b->size);
    return clone;
}

INLINE void b_prepare(blob_t *b,size_t size) {
    if (b->size - b->pos >= size)
        return;
    b->size += size;
    b->data = realloc_or_die(b->data,b->size);
}

INLINE void b_set_pos(blob_t *b, unsigned int pos) {
    if (pos > b->size)
        SAYX(EXIT_FAILURE,"offset of %u > blob size(%u) [ blob current pos(%u) ] ",pos,b->size,b->pos);
    b->pos = pos;
}

INLINE void b_shift(blob_t *b,unsigned int len) {
    b_set_pos(b,b->pos + len);
}

INLINE char * b_data_at_pos(blob_t *b, unsigned int pos) {
    if (pos > b->size)
        SAYX(EXIT_FAILURE,"pos(%u) > b->size(%u)",pos,b->size);
    return (b->data + pos);
}

INLINE char *b_data(blob_t *b) {
    return b_data_at_pos(b,b->pos);
}

void b_init_static(void) {
}

INLINE void b_destroy(blob_t *b) {
    free(b->data);
    free(b);
}

void b_destroy_static(void) {
}
