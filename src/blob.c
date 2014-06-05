#include "blob.h"

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

    b = malloc_or_die(sizeof(blob_t));
    b->pos = 0;
    b->next = NULL;
    b->ref = malloc_or_die(sizeof(_refcnt_blob_t));
    LOCK_INIT(&b->ref->lock);
    b->ref->refcnt = 1;
    b->ref->data= NULL;
    return b;
}

INLINE blob_t * b_clone(blob_t *b) {
    blob_t *clone;

    clone= malloc_or_die(sizeof(blob_t));
    clone->pos= b->pos;
    clone->next= NULL;

    /* note we assume that b->ref->refcnt is setup externally to b_clone */
    clone->ref= b->ref;

    return clone;
}

INLINE void b_prepare(blob_t *b,size_t size) {
    if (!b->ref)
        SAYX(EXIT_FAILURE,"b->ref is null"); /* XXX */
    if (!b->ref->data) {
        b->ref->data= malloc_or_die(sizeof(__data_blob_t) + size);
        b->ref->data->size= size;
    } else {
        if (b->ref->data->size - b->pos >= size)
            return;
        size += b->ref->data->size;
        b->ref->data = realloc_or_die(b->ref->data, size);
        b->ref->data->size= size;
    }
}

INLINE void b_set_pos(blob_t *b, unsigned int pos) {
    if (!b->ref)
        SAYX(EXIT_FAILURE,"b->ref is null"); /* XXX */
    if (!b->ref->data)
        SAYX(EXIT_FAILURE,"b->ref->data is null"); /* XXX */
    if (pos > b->ref->data->size)
        SAYX(EXIT_FAILURE,"offset of %u > blob size(%u) [ blob current pos(%u) ] ",pos,b->ref->data->size,b->pos);
    b->pos = pos;
}

INLINE void b_shift(blob_t *b,unsigned int len) {
    b_set_pos(b,b->pos + len);
}

INLINE char * b_data_at_pos(blob_t *b, unsigned int pos) {
    if (!b->ref)
        SAYX(EXIT_FAILURE,"b->ref is null"); /* XXX */
    if (!b->ref->data)
        SAYX(EXIT_FAILURE,"b->ref->data is null"); /* XXX */
    if (pos > b->ref->data->size)
        SAYX(EXIT_FAILURE,"pos(%u) > b->size(%u)",pos,b->ref->data->size);
    return (b->ref->data->data + pos);
}

INLINE char *b_data(blob_t *b) {
    return b_data_at_pos(b,b->pos);
}


INLINE void b_destroy(blob_t *b) {
    if ( b->ref ) {
        uint32_t refcnt;
        LOCK(&b->ref->lock);
        refcnt= b->ref->refcnt;
        if (refcnt)
            b->ref->refcnt--;
        UNLOCK(&b->ref->lock);
        if (refcnt == 1) {
            /* we were the last owner so we can release it */
            free(b->ref->data);
            LOCK_DESTROY(&b->ref->lock);
            free(b->ref);
        }
    }
    free(b);
}

void b_init_static(void) {}
void b_destroy_static(void) {}
