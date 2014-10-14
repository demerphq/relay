#ifndef RELAY_STRING_UTIL_H
#define RELAY_STRING_UTIL_H

#include <sys/types.h>

/* Replaces any non-alphanumeric bytes with an underscore. */
void scrub_nonalnum(char* str, size_t size);

/* Trims leading and trailing whitespace. */
void trim_space(char* str);

#endif				/* #ifndef RELAY_STRING_UTIL_H */


