#include "blob.h"
/* reallocate a buffer or die - (wonder if we should be more graceful
 * when we shutdown? */
void *realloc_or_die(void *p, size_t size) {
    p = realloc(p, size);
    if (!p)
        DIE_RC(EXIT_FAILURE, "unable to allocate %zu bytes", size);
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
    (void)get_time(&BLOB_RECEIVED_TIME(b));

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


/* append an item to queue non-safely */
uint32_t q_append_nolock(queue_t *q, blob_t *b) {
    if (q->head == NULL)
        q->head = b;
    else
        BLOB_NEXT_set(q->tail,b);

    q->tail = b;
    BLOB_NEXT_set(b,NULL);
    return ++(q->count);
}

/* append an item, optionally locked */
uint32_t q_append(queue_t *q, blob_t *b, LOCK_T *lock) {
    uint32_t count;
    LOCK(lock);
    count= q_append_nolock(q, b);
    UNLOCK(lock);
    return count;
}

/* shift an item out of a queue non-safely */
blob_t *q_shift_nolock(queue_t *q) {
    blob_t *b= q->head;
    if (b) {
        if (BLOB_NEXT(b))
            q->head = BLOB_NEXT(b);
        else
            q->head = q->tail = NULL;
        q->count--;
    }
    return b;
}

/* shift an item out of a queue, optionally locked*/
blob_t *q_shift(queue_t *q, LOCK_T *lock) {
    blob_t *b;
    LOCK(lock);
    b= q_shift_nolock(q);
    UNLOCK(lock);
    return b;
}

/* hijack a queue into a separate structure, return the number of items
 * hijacked */
uint32_t q_hijack_nolock(queue_t *q, queue_t *hijacked_queue) {
    memcpy(hijacked_queue, q, sizeof(queue_t));
    q->tail = q->head = NULL;
    q->count = 0;
    return hijacked_queue->count;
}

/* hijack a queue with optional locks */
uint32_t q_hijack(queue_t *q, queue_t *hijacked_queue, LOCK_T *lock) {
    uint32_t count;
    LOCK(lock);
    count= q_hijack_nolock(q, hijacked_queue);
    UNLOCK(lock);
    return count;
}


