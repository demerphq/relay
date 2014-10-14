#ifndef RELAY_STRING_UTIL_H
#define RELAY_STRING_UTIL_H

#include <sys/types.h>

/* In the str replaces any non-alphanumeric bytes with an underscore. */
void scrub_nonalnum(char* str, size_t size);

#endif				/* #ifndef RELAY_STRING_UTIL_H */


