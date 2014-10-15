#ifndef RELAY_STRING_UTIL_H
#define RELAY_STRING_UTIL_H

#include <sys/types.h>

/* Replaces any non-alphanumeric bytes with an underscore. */
void scrub_nonalnum(char *str, size_t size);

/* Trims leading and trailing whitespace. */
void trim_space(char *str);

#define STREQ(a, b) (strcmp((a),(b))==0)
#define STRNE(a, b) (strcmp((a),(b))!=0)

#endif				/* #ifndef RELAY_STRING_UTIL_H */
