#include "string_util.h"

#include <ctype.h>
#include <string.h>

void scrub_nonalnum(char* str, size_t size)
{
    char* p = str;
    char* e = p + size;
    char c;
    while (p < e && (c = *p)) {
	*p++ = isalnum(c) ? c : '_';
    }
}

void trim_space(char *s)
{
    char *p = s;
    int l = strlen(p);

    while (isspace(p[l - 1]))
	p[--l] = 0;
    while (*p && isspace(*p))
	++p, --l;

    memmove(s, p, l + 1);
}
