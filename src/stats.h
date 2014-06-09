#ifndef _STATS_H
#define _STATS_H

#include "relay_common.h"
#include "relay_threads.h"
#include "setproctitle.h"
#include <stdio.h>


#define STATSfmt "%lu"
typedef uint64_t stats_count_t;

struct stats_basic_counters {
    volatile stats_count_t count;
    volatile stats_count_t total;
    volatile stats_count_t per_second;
};
typedef struct stats_basic_counters stats_basic_counters_t;

void inc_stats_count(stats_basic_counters_t *stats);
stats_count_t snapshot_stats(stats_basic_counters_t *stats, stats_count_t *total_addr);

#endif
