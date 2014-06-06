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
    LOCK_T lock;
    uint32_t refcnt;
    uint32_t size;
    char data[0];
} __attribute__ ((packed));

/* this structure is private to each thread */
struct blob {
    struct blob *next;
    struct __data_blob *ref;
};
typedef struct blob blob_t;

/* blob.c */
void *realloc_or_die(void *p, size_t size);
void *malloc_or_die(size_t size);
blob_t * b_new(size_t size);
blob_t * b_clone(blob_t *b);
void b_destroy(blob_t *b);
#endif
