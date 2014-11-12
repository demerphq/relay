#ifndef RELAY_STRING_UTIL_H
#define RELAY_STRING_UTIL_H

#include <stdarg.h>
#include <sys/types.h>

#include "relay_common.h"

/* Replaces any non-alphanumeric bytes with an underscore. */
void underscorify_nonalnum(char *str, size_t size);

/* Trims leading and trailing whitespace. */
void trim_space(char *str);

#define STREQ(a, b) (strcmp((a),(b))==0)
#define STRNE(a, b) (strcmp((a),(b))!=0)

typedef struct {
    ssize_t size;
    ssize_t used;
    char data[0];		/* Data inlined with the header. */
} fixed_buffer_t;

fixed_buffer_t *fixed_buffer_create(size_t size);

int fixed_buffer_vcatf(fixed_buffer_t * buf, const char *fmt, ...);

INLINE static void fixed_buffer_reset(fixed_buffer_t * buf)
{
    buf->used = 0;
}

INLINE static void fixed_buffer_zero_terminate(fixed_buffer_t * buf)
{
    buf->data[buf->used < buf->size ? buf->used : buf->size - 1] = 0;
}

void fixed_buffer_destroy(fixed_buffer_t * buf);

#endif				/* #ifndef RELAY_STRING_UTIL_H */
