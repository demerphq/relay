#include "string_util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void underscorify_nonalnum(char *str, size_t size)
{
    char *p = str;
    char *e = p + size;
    char c;
    while (p < e && (c = *p)) {
	*p++ = isalnum(c) ? c : '_';
    }
}

void trim_space(char *s)
{
    if (*s == 0)
	return;

    char *p = s;

    while (isspace(*p))
	p++;
    if (*p == 0) {
	if (p > s)
	    *s = 0;
	return;
    }

    char *q = s;
    while (*p)
	*q++ = *p++;
    p--;
    if (isspace(*p)) {
	while (isspace(*p))
	    p--;
	p++;
	*p = 0;
    } else
	*q = 0;
}

void reverse_dotwise(char *str)
{
    char *p = str;
    char *q = str;
    char *r;
    /* Find the end. */
    while (*q)
	q++;
    r = q;
    /* Reverse the whole string. */
    for (q--; p < q; p++, q--) {
	char t = *p;
	*p = *q;
	*q = t;
    }
    /* Reverse the elements. */
    for (p = q = str; p <= r; p++) {
	if (*p == '.' || *p == 0) {
	    for (char *s = p - 1; q < s; q++, s--) {
		char t = *s;
		*s = *q;
		*q = t;
	    }
	    q = p + 1;
	}
    }
}

fixed_buffer_t *fixed_buffer_create(size_t size)
{
    fixed_buffer_t *b = (fixed_buffer_t *) malloc(sizeof(fixed_buffer_t) + size);
    b->size = size;
    b->used = 0;
    return b;
}

int fixed_buffer_vcatf(fixed_buffer_t * buf, const char *fmt, ...)
{
    ssize_t len = buf->size - buf->used;
    if (len < 1)
	return 0;
    va_list ap;
    va_start(ap, fmt);
    int wrote = vsnprintf(buf->data + buf->used, len, fmt, ap);
    va_end(ap);
    if (wrote < 0 || wrote >= len)
	return 0;
    buf->used += wrote;
    return wrote;
}

void fixed_buffer_destroy(fixed_buffer_t * buf)
{
    buf->used = 0;
    buf->size = 0;
    free(buf);
}
