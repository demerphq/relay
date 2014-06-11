#include "blob.h"

/* reallocate a buffer or die - (wonder if we should be more graceful
 * when we shutdown? */
void *realloc_or_die(void *p, size_t size) {
    p = realloc(p, size);
    if (!p)
        SAYX(EXIT_FAILURE, "unable to allocate %zu bytes", size);
    return p;
}

/* malloc a buffer or die - via realloc_or_die() */
void *malloc_or_die(size_t size) {
    return realloc_or_die(NULL, size);
}

/* blob= b_new(size) - create a new empty blob with space for size bytes */
INLINE blob_t * b_new(size_t size) {
    blob_t *b;

    b = malloc_or_die(sizeof(blob_t));
    b->fallback = NULL;
    BLOB_NEXT_set(b, NULL);
    BLOB_REF_PTR_set(b, malloc_or_die(sizeof(_refcnt_blob_t) + size));
    BLOB_REFCNT_set(b, 1); /* overwritten in enqueue_blob_for_transmision */
    BLOB_BUF_SIZE_set(b, size);

    return b;
}

/* blob= b_clone_no_refcnt_inc(a_blob) - lightweight clone of the original
 * note the refcount of the underlying _refcnt_blob_t is NOT
 * incremented, that must be done externally. */
INLINE blob_t * b_clone_no_refcnt_inc(blob_t *b) {
    blob_t *clone;

    clone= malloc_or_die(sizeof(blob_t));
    clone->fallback = NULL;
    BLOB_NEXT_set(clone, NULL);

    /* Note we assume that BLOB_REFCNT(b) is setup externally
     * so we do NOT set the refcnt when we do this.
     *
     * This also avoid unnecessary lock churn.
     */
    BLOB_REF_PTR_set(clone, BLOB_REF_PTR(b));

    return clone;
}

/* b_destroy(blob) - destroy a blob object */
void b_destroy(blob_t *b) {
    if ( BLOB_REF_PTR(b) ) {
        int32_t refcnt= RELAY_ATOMIC_DECREMENT(BLOB_REFCNT(b),1);
        if (refcnt <= 1) {
            /* we were the last owner so we can release it */
            free(BLOB_REF_PTR(b));
        }
    }
    free(b->fallback);
    free(b);
}
