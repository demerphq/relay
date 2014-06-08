#ifndef _STATS_H
#define _STATS_H

#include "relay_common.h"
#include "relay_threads.h"
#include "setproctitle.h"
#include <stdio.h>

#define MAX_BUF_LEN 128

#define STATSfmt "%lu"
typedef uint64_t stats_count_t;
struct __stats { 
    stats_count_t   packet_count; 
    stats_count_t   packets_per_second;
    stats_count_t   accumulated;
};
typedef struct __stats stats_t;
void inc_packets();
void reset_packets();

#endif
