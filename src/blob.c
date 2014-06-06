#include "blob.h"

void *realloc_or_die(void *p, size_t size) {
    p = realloc(p,size);
    if (!p)
        SAYX(EXIT_FAILURE,"unable to allocate %zu bytes",size);
    return p;
}
void *malloc_or_die(size_t size) {
    return realloc_or_die(NULL,size);
}

INLINE blob_t * b_new(size_t size) {
    blob_t *b;
    b = malloc_or_die(sizeof(blob_t));
    b->next = NULL;
    b->ref = malloc_or_die(sizeof(struct __data_blob) + size);
    LOCK_INIT(&b->ref->lock);
    b->ref->size = size;
    b->ref->refcnt = 0;
    return b;
}

INLINE blob_t * b_clone(blob_t *b) {
    blob_t *clone;

    clone= malloc_or_die(sizeof(blob_t));
    clone->next = NULL;
    clone->ref = b->ref;

    return clone;
}

void b_destroy(blob_t *b) {
    if (b->ref) {
        LOCK(&b->ref->lock);
        if (b->ref->refcnt == 0 || --b->ref->refcnt == 0) {
            UNLOCK(&b->ref->lock);
            LOCK_DESTROY(&b->ref->lock);
            free(b->ref);
        } else {
            UNLOCK(&b->ref->lock);
        }
    }
    free(b);
}
