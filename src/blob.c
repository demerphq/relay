#include "blob.h"


INLINE blob_t * b_new(void) {
    blob_t *b;

    b = malloc_or_die(sizeof(blob_t));
    BLOB_NEXT_set(b, NULL);
    BLOB_REF_PTR_set(b, malloc_or_die(sizeof(_refcnt_blob_t)));
    LOCK_INIT(&BLOB_LOCK(b));
    BLOB_REFCNT_set(b, 1); /* XXX: workers? */
    BLOB_DATA_PTR_set(b, NULL);
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

void b_prepare(blob_t *b,size_t size) {
    if (!BLOB_REF_PTR(b))
        SAYX(EXIT_FAILURE,"BLOB_REF_PTR(b) is null"); /* XXX */
    if (!BLOB_DATA_PTR(b)) {
        BLOB_DATA_PTR_set(b, malloc_or_die(sizeof(__data_blob_t) + size));
        BLOB_SIZE_set(b, size);
    } else {
        size += BLOB_SIZE(b);
        BLOB_DATA_PTR_set(b,realloc_or_die(BLOB_DATA_PTR(b), size));
        BLOB_SIZE_set(b, size);
    }
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

void b_init_static(void) {}
void b_destroy_static(void) {}
