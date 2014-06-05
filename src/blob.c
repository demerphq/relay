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
    BLOB_NEXT_set(b,NULL);
    BLOB_REF_PTR(b) = malloc_or_die(sizeof(_refcnt_blob_t));
    LOCK_INIT(&BLOB_LOCK(b));
    BLOB_REFCNT(b) = 1;
    BLOB_DATA_PTR(b)= NULL;
    return b;
}

INLINE blob_t * b_clone(blob_t *b) {
    blob_t *clone;

    clone= malloc_or_die(sizeof(blob_t));
    BLOB_NEXT_set(clone,NULL);

    /* note we assume that BLOB_REFCNT(b) is setup externally to b_clone */
    BLOB_REF_PTR(clone)= BLOB_REF_PTR(b);

    return clone;
}

INLINE void b_prepare(blob_t *b,size_t size) {
    if (!BLOB_REF_PTR(b))
        SAYX(EXIT_FAILURE,"BLOB_REF_PTR(b) is null"); /* XXX */
    if (!BLOB_DATA_PTR(b)) {
        BLOB_DATA_PTR(b)= malloc_or_die(sizeof(__data_blob_t) + size);
        BLOB_SIZE(b)= size;
    } else {
        size += BLOB_SIZE(b);
        BLOB_DATA_PTR(b) = realloc_or_die(BLOB_DATA_PTR(b), size);
        BLOB_SIZE(b)= size;
    }
}

INLINE void b_destroy(blob_t *b) {
    if ( BLOB_REF_PTR(b) ) {
        uint32_t refcnt;
        LOCK(&BLOB_LOCK(b));
        refcnt= BLOB_REFCNT(b);
        if (refcnt)
            BLOB_REFCNT(b)--;
        UNLOCK(&BLOB_LOCK(b));
        if (refcnt == 1) {
            /* we were the last owner so we can release it */
            free(BLOB_DATA_PTR(b));
            LOCK_DESTROY(&BLOB_LOCK(b));
            free(BLOB_REF_PTR(b));
        }
    }
    free(b);
}

void b_init_static(void) {}
void b_destroy_static(void) {}
