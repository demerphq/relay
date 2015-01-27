#include "blob.h"

#include "log.h"
#include "relay_threads.h"
#include "timer.h"

/* reallocate a buffer or die - (wonder if we should be more graceful
 * when we shutdown? */
void *realloc_or_fatal(void *p, size_t size)
{
    p = realloc(p, size);
    if (!p)
        FATAL("Unable to allocate %zu bytes", size);
    return p;
}

/* malloc a buffer or die - via realloc_or_fatal() */
void *malloc_or_fatal(size_t size)
{
    return realloc_or_fatal(NULL, size);
}

/* malloc a buffer or die - via realloc_or_fatal() */
void *calloc_or_fatal(size_t size)
{
    void *ptr = realloc_or_fatal(NULL, size);
    memset(ptr, 0, size);
    return ptr;
}

/* NOTE: only really handles allocs of up to 1<<32!
 * Would need a way to "count leading zeros" on 64-bit. */
static void inc_blob_total_sizes(size_t size)
{
    int bucket = size ? (32 - __builtin_clz((int) size - 1)) : 0;
    if (bucket < 0 || bucket > (int) sizeof(GLOBAL.blob_total_sizes) / (int) sizeof(GLOBAL.blob_total_sizes[0])) {
        return;                 /* should die, really */
    }
    RELAY_ATOMIC_OR(GLOBAL.blob_total_ored_buckets, 1 << bucket);
    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_total_sizes[bucket], 1);
}

/* blob= blob_new(size) - create a new empty blob with space for size bytes */
INLINE blob_t *blob_new(size_t size)
{
    blob_t *b;
    size_t refcnt_size;

    b = malloc_or_fatal(sizeof(blob_t));
    BLOB_NEXT_set(b, NULL);
    refcnt_size = sizeof(refcnt_blob_t) + size;
    BLOB_REF_PTR_set(b, malloc_or_fatal(refcnt_size));
    BLOB_REFCNT_set(b, 1);      /* overwritten in enqueue_blob_for_transmision */
    BLOB_BUF_SIZE_set(b, size);

    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_active_count, 1);
    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_active_bytes, sizeof(blob_t));
    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_active_refcnt_bytes, refcnt_size);
    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_total_count, 1);
    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_total_bytes, sizeof(blob_t));
    RELAY_ATOMIC_INCREMENT(GLOBAL.blob_total_refcnt_bytes, refcnt_size);

    inc_blob_total_sizes(sizeof(blob_t));
    inc_blob_total_sizes(refcnt_size);

    (void) get_time(&BLOB_RECEIVED_TIME(b));

    return b;
}

/* blob= blob_clone_no_refcnt_inc(a_blob) - lightweight clone of the original
 * note the refcount of the underlying _refcnt_blob_t is NOT
 * incremented, that must be done externally. */
INLINE blob_t *blob_clone_no_refcnt_inc(blob_t * b)
{
    blob_t *cloned = malloc_or_fatal(sizeof(blob_t));
    BLOB_NEXT_set(cloned, NULL);

    /* Note we assume that BLOB_REFCNT(b) is setup externally
     * so we do NOT set the refcnt when we do this.
     *
     * This also avoid unnecessary lock churn.
     */
    BLOB_REF_PTR_set(cloned, BLOB_REF_PTR(b));

    return cloned;
}

/* blob_destroy(blob) - destroy a blob object */
void blob_destroy(blob_t * b)
{
    if (BLOB_REF_PTR(b)) {
        int32_t refcnt = RELAY_ATOMIC_DECREMENT(BLOB_REFCNT(b), 1);
        if (refcnt <= 1) {
            /* we were the last owner so we can release it */
            RELAY_ATOMIC_DECREMENT(GLOBAL.blob_active_refcnt_bytes, sizeof(refcnt_blob_t) + BLOB_BUF_SIZE(b));
            free(BLOB_REF_PTR(b));
        }
    }
    RELAY_ATOMIC_DECREMENT(GLOBAL.blob_active_bytes, sizeof(blob_t));
    RELAY_ATOMIC_DECREMENT(GLOBAL.blob_active_count, 1);
    free(b);
}


/* append an item to queue non-safely */
uint32_t queue_append_nolock(queue_t * q, blob_t * b)
{
    if (q->head == NULL)
        q->head = b;
    else
        BLOB_NEXT_set(q->tail, b);

    q->tail = b;
    BLOB_NEXT_set(b, NULL);
    return ++(q->count);
}

uint32_t queue_append_tail_nolock(queue_t * q, queue_t * tail)
{

    if (q->head == NULL)
        q->head = tail->head;
    else
        BLOB_NEXT_set(q->tail, tail->head);

    q->tail = tail->tail;
    q->count += tail->count;

    tail->head = NULL;
    tail->tail = NULL;
    tail->count = 0;

    return q->count;
}

uint32_t queue_append_tail(queue_t * q, queue_t * tail, LOCK_T * lock)
{
    uint32_t count;
    LOCK(lock);
    count = queue_append_tail_nolock(q, tail);
    UNLOCK(lock);
    return count;
}

uint32_t queue_append(queue_t * q, blob_t * b, LOCK_T * lock)
{
    uint32_t count;
    LOCK(lock);
    count = queue_append_nolock(q, b);
    UNLOCK(lock);
    return count;
}

/* shift an item out of a queue non-safely */
blob_t *queue_shift_nolock(queue_t * q)
{
    blob_t *b = q->head;
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
blob_t *queue_shift(queue_t * q, LOCK_T * lock)
{
    blob_t *b;
    LOCK(lock);
    b = queue_shift_nolock(q);
    UNLOCK(lock);
    return b;
}

/* hijack a queue into a separate structure, return the number of items
 * hijacked */
uint32_t queue_hijack_nolock(queue_t * q, queue_t * hijacked_queue)
{
    memcpy(hijacked_queue, q, sizeof(queue_t));
    q->tail = q->head = NULL;
    q->count = 0;
    return hijacked_queue->count;
}

/* hijack a queue with optional locks */
uint32_t queue_hijack(queue_t * q, queue_t * hijacked_queue, LOCK_T * lock)
{
    uint32_t count;
    LOCK(lock);
    count = queue_hijack_nolock(q, hijacked_queue);
    UNLOCK(lock);
    return count;
}
