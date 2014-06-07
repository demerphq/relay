#ifndef _BLOB_H
#define _BLOB_H

#include "relay_common.h"
#include "relay_threads.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>

/* this structure is shared between different threads */
/* the idea here is that we want a data structure which is exactly
 * 4 bytes of length, followed by K bytes of string */
struct __data_blob {
    uint32_t size;
    char data[0];
} __attribute__ ((packed));
typedef struct __data_blob __data_blob_t;

/* this structure is shared between different threads
 * we use this to refcount __blob_t items, and we use
 * the lock to guard refcnt modifications */
struct _refcnt_blob {
    LOCK_T lock;
    uint32_t refcnt;
    __data_blob_t *data;
};
typedef struct _refcnt_blob _refcnt_blob_t;

/* this structure is private to each thread */
struct blob {
    struct blob *next;
    _refcnt_blob_t *ref;
};
typedef struct blob blob_t;

#define BLOB_REF_PTR(B)     ((B)->ref)
#define BLOB_NEXT(B)        ((B)->next)

#define BLOB_DATA_PTR(B)    (BLOB_REF_PTR(B)->data)

#define BLOB_REFCNT(B)      (BLOB_REF_PTR(B)->refcnt)
#define BLOB_LOCK(B)        (BLOB_REF_PTR(B)->lock)

#define BLOB_DATA_SIZE(B)   (BLOB_DATA_PTR(B)->size + sizeof(BLOB_DATA_PTR(B)->size))

#define BLOB_SIZE(B)        (BLOB_DATA_PTR(B)->size)
#define BLOB_DATA(B)        (BLOB_DATA_PTR(B)->data)

/* --- */

#define BLOB_REF_PTR_set(B, v) (B)->ref= (v)
#define BLOB_NEXT_set(B, v)  (B)->next = (v)

#define BLOB_DATA_PTR_set(B, v) BLOB_REF_PTR(B)->data = (v)

#define BLOB_REFCNT_set(B, v)  BLOB_REF_PTR(B)->refcnt = (v)
#define BLOB_REFCNT_dec(B)  BLOB_REF_PTR(B)->refcnt--

#define BLOB_LOCK_set(B, v)    BLOB_REF_PTR(B)->lock = (v)
#define BLOB_SIZE_set(B, v)    BLOB_DATA_PTR(B)->size = (v)

//#define BLOB_DATA_set(B)        (BLOB_DATA_PTR(B)->data)

/* blob.c */
void *realloc_or_die(void *p, size_t size);
void *malloc_or_die(size_t size);
blob_t * b_new(size_t size);
blob_t * b_clone(blob_t *b);
void b_destroy(blob_t *b);
#endif
