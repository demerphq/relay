#include "string_util.h"

#include <ctype.h>

void scrub_nonalnum(char* str, size_t size)
{
    char* p = str;
    char* e = p + size;
    char c;
    while (p < e && (c = *p)) {
	*p++ = isalnum(c) ? c : '_';
    }
}


