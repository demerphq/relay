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
    BLOB_NEXT_set(b, NULL);
    BLOB_REF_PTR_set(b, malloc_or_die(sizeof(_refcnt_blob_t)));
    BLOB_REFCNT_set(b, 1); /* XXX: should we set it to the number workers here */
    LOCK_INIT(&BLOB_LOCK(b));

    BLOB_DATA_PTR_set(b, malloc_or_die(sizeof(__data_blob_t) + size));
    BLOB_SIZE_set(b, size);

    return b;
}

INLINE blob_t * b_clone(blob_t *b) {
    blob_t *clone;

    clone= malloc_or_die(sizeof(blob_t));
    BLOB_NEXT_set(clone,NULL);

    /* note we assume that BLOB_REFCNT(b) is setup externally to b_clone */
    BLOB_REF_PTR_set(clone,BLOB_REF_PTR(b));

    return clone;
}

void b_destroy(blob_t *b) {
    if ( BLOB_REF_PTR(b) ) {
        uint32_t refcnt;
        LOCK(&BLOB_LOCK(b));
        refcnt= BLOB_REFCNT(b);
        if (refcnt)
            BLOB_REFCNT_dec(b);
        UNLOCK(&BLOB_LOCK(b));
        if (refcnt <= 1) {
            /* we were the last owner so we can release it */
            free(BLOB_DATA_PTR(b));
            LOCK_DESTROY(&BLOB_LOCK(b));
            free(BLOB_REF_PTR(b));
        }
    }
    free(b);
}
