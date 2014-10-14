#ifndef RELAY_TIMER_H
#define RELAY_TIMER_H

#include <sys/time.h>
#include <stdint.h>

int get_time(struct timeval *t);
uint64_t elapsed_usec(struct timeval *start_time, struct timeval *end_time);

#endif				/* #ifndef RELAY_TIMER_H */
