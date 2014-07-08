#include "timer.h"


int get_time( mytime_t *t) {
    return gettimeofday(t, 0);
}

uint64_t elapsed_usec(mytime_t *start_time, mytime_t *end_time) {
    return ( ( end_time->tv_sec - start_time->tv_sec ) * 1000000 )
           + end_time->tv_usec - start_time->tv_usec;
}
