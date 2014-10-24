#include "string_util.h"

#include <ctype.h>
#include <string.h>

void scrub_nonalnum(char *str, size_t size)
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
