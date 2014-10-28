#ifndef RELAY_STRING_UTIL_H
#define RELAY_STRING_UTIL_H

#include <stdarg.h>
#include <sys/types.h>

/* Replaces any non-alphanumeric bytes with an underscore. */
void scrub_nonalnum(char *str, size_t size);

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
void fixed_buffer_destroy(fixed_buffer_t * buf);

#endif				/* #ifndef RELAY_STRING_UTIL_H */
