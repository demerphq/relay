#ifndef _BLOB_H
#define _BLOB_H

#include "relay_common.h"
#include "relay_threads.h"
#include "timer.h"
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

/* this structure is shared between different threads */
/* the idea here is that we want a data structure which is exactly
 * 4 bytes of length, followed by K bytes of string */
struct __data_blob {
    uint32_t size;
    char buf[0];
} __attribute__ ((packed));
typedef struct __data_blob __data_blob_t;

/* this structure is shared between different threads
 * we use this to refcount __blob_t items, and we use
 * the lock to guard refcnt modifications */
struct _refcnt_blob {
    volatile int32_t refcnt;
    mytime_t received_time;
    __data_blob_t data;
};
typedef struct _refcnt_blob _refcnt_blob_t;

/* this structure is private to each thread */
struct blob {
    struct blob *next;
    _refcnt_blob_t *ref;
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

//#define BLOB_BUF_set(B)        (BLOB_DATA_PTR(B)->data)

/* blob.c */
void *realloc_or_die(void *p, size_t size);
void *malloc_or_die(size_t size);
void *mallocz_or_die(size_t size);
blob_t *blob_new(size_t size);
blob_t *blob_clone_no_refcnt_inc(blob_t * b);
void blob_destroy(blob_t * b);

/* queue stuff */
uint32_t q_append_nolock(queue_t * q, blob_t * b);
uint32_t q_append_q_nolock(queue_t * q, queue_t * tail);
blob_t *q_shift_nolock(queue_t * q);
uint32_t q_hijack_nolock(queue_t * q, queue_t * hijacked_queue);

uint32_t q_append(queue_t * q, blob_t * b, LOCK_T * lock);
uint32_t q_append_q(queue_t * q, queue_t * tail, LOCK_T * lock);
blob_t *q_shift(queue_t * q, LOCK_T * lock);
uint32_t q_hijack(queue_t * q, queue_t * hijacked_queue, LOCK_T * lock);




#endif
