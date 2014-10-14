#include "timer.h"


int get_time(struct timeval *t)
{
    return gettimeofday(t, 0);
}

uint64_t elapsed_usec(struct timeval * start_time, struct timeval * end_time)
{
    return ((end_time->tv_sec - start_time->tv_sec) * 1000000)
	+ end_time->tv_usec - start_time->tv_usec;
}
