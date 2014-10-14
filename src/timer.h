#ifndef RELAY_TIMER_H
#define RELAY_TIMER_H

#include <sys/time.h>
#include <stdint.h>
typedef struct timeval mytime_t;

int get_time(mytime_t * t);
uint64_t elapsed_usec(mytime_t * start_time, mytime_t * end_time);

#endif /* #ifndef RELAY_TIMER_H */
