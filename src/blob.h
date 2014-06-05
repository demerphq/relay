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


/* blob.c */
INLINE void *realloc_or_die(void *p, size_t size);
INLINE void *malloc_or_die(size_t size);
INLINE blob_t * b_new(void);
INLINE blob_t * b_clone(blob_t *b);
INLINE void b_prepare(blob_t *b,size_t size);
INLINE void b_destroy(blob_t *b);
void b_init_static(void);
void b_destroy_static(void);
#endif
