#include "timer.h"

int get_time(struct timeval *t)
{
    return gettimeofday(t, 0);
}

uint64_t elapsed_usec(const struct timeval * start_time, const struct timeval * end_time)
{
    return (uint64_t) ((end_time->tv_sec - start_time->tv_sec) * 1000000)
	+ (uint64_t) end_time->tv_usec - (uint64_t) start_time->tv_usec;
}
