#ifndef RELAY_BLOB_H
#define RELAY_BLOB_H

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

#include "relay_threads.h"

/* The size of the blob.  Note that the wire format is LITTLE-ENDIAN. */
typedef uint32_t blob_size_t;

/* The below makes builds on non-x86 fail.  It is slightly overzealous
 * in that other little-endian will fail, too, unnecessarily.
 *
 * What would need to be changed for big-endian:
 * (1) before sendto() the size would need swapping to little-endian.
 * (2) after sendto() the size would need unswapping.
 * (3) after recv() the size would need swapping from little-endian.
 */
#if !(defined(__x86_64__) || defined(__x86__))
#error "not x86, code needs porting"
#endif

/* this structure is shared between different threads */
/* the idea here is that we want a data structure which is exactly
 * 4 bytes of length, followed by K bytes of string */
struct data_blob {
    blob_size_t size;
    /* "unwarranted chumminess with the C implementation":
     * it's the technical term, look it up. */
    char buf[0];
} __attribute__ ((packed));
typedef struct data_blob data_blob_t;

/* this structure is shared between different threads
 * we use this to refcount __blob_t items, and we use
 * the lock to guard refcnt modifications */
struct refcnt_blob {
    volatile int32_t refcnt;
    struct timeval received_time;
    data_blob_t data;
};
typedef struct refcnt_blob refcnt_blob_t;

/* this structure is private to each thread */
struct blob {
    struct blob *next;
    refcnt_blob_t *ref;
};
typedef struct blob blob_t;

struct queue {
    LOCK_T lock;
    blob_t *head;
    blob_t *tail;
    uint32_t count;
};
typedef struct queue queue_t;

#define BLOB_REF_PTR(B)             ((B)->ref)
#define BLOB_NEXT(B)                ((B)->next)

#define BLOB_DATA_MBR(B)            (BLOB_REF_PTR(B)->data)
#define BLOB_DATA_MBR_addr(B)       (&BLOB_DATA_MBR(B))

#define BLOB_REFCNT(B)              (BLOB_REF_PTR(B)->refcnt)
#define BLOB_RECEIVED_TIME(B)       (BLOB_REF_PTR(B)->received_time)

#define BLOB_BUF_SIZE(B)            (BLOB_DATA_MBR(B).size)
#define BLOB_BUF(B)                 (BLOB_DATA_MBR(B).buf)
#define BLOB_BUF_addr(B)            (&BLOB_BUF(B))

#define BLOB_DATA_MBR_SIZE(B)       (BLOB_BUF_SIZE(B) + sizeof(BLOB_BUF_SIZE(B)))
/* --- */

#define BLOB_REF_PTR_set(B, v)      (B)->ref= (v)
#define BLOB_NEXT_set(B, v)         (B)->next = (v)

#define BLOB_REFCNT_set(B, v)       BLOB_REF_PTR(B)->refcnt = (v)
#define BLOB_REFCNT_dec(B)          BLOB_REF_PTR(B)->refcnt--
#define BLOB_REFCNT_inc(B)          BLOB_REF_PTR(B)->refcnt++

#define BLOB_LOCK_set(B, v)         BLOB_REF_PTR(B)->lock = (v)
#define BLOB_BUF_SIZE_set(B, v)     BLOB_DATA_MBR(B).size = (v)

/* blob.c */
void *realloc_or_fatal(void *p, size_t size);
void *malloc_or_fatal(size_t size);
void *calloc_or_fatal(size_t size);
blob_t *blob_new(size_t size);
blob_t *blob_clone_no_refcnt_inc(blob_t * b);
void blob_destroy(blob_t * b);

/* queue stuff */
uint32_t queue_append_nolock(queue_t * q, blob_t * b);
uint32_t queue_append_tail_nolock(queue_t * q, queue_t * tail);
blob_t *queue_shift_nolock(queue_t * q);
uint32_t queue_hijack_nolock(queue_t * q, queue_t * hijacked_queue);

uint32_t queue_append(queue_t * q, blob_t * b, LOCK_T * lock);
uint32_t queue_append_tail(queue_t * q, queue_t * tail, LOCK_T * lock);
blob_t *queue_shift(queue_t * q, LOCK_T * lock);
uint32_t queue_hijack(queue_t * q, queue_t * hijacked_queue, LOCK_T * lock);

#endif                          /* #ifndef RELAY_BLOB_H */
