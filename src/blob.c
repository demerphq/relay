#include "blob.h"

INLINE void *realloc_or_die(void *p, size_t size) {
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
        size += b->ref->data->size;
        b->ref->data = realloc_or_die(b->ref->data, size);
        b->ref->data->size= size;
    }
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
