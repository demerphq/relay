#ifndef RELAY_TIMER_H
#define RELAY_TIMER_H

#include <stdint.h>
#include <sys/time.h>

int get_time(struct timeval *t);
uint64_t elapsed_usec(const struct timeval *start_time, const struct timeval *end_time);

#endif				/* #ifndef RELAY_TIMER_H */
