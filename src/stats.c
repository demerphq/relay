#include "stats.h"



void inc_stats_count(stats_basic_counters_t *stats) {
    RELAY_ATOMIC_INCREMENT(stats->count,1);
}

stats_count_t
snapshot_stats(stats_basic_counters_t *stats, stats_count_t *total_addr) {
    stats_count_t count= RELAY_ATOMIC_READ(stats->count);
    stats_count_t total= count;
    
    /* and now remove the amount fetched from the counter */
    RELAY_ATOMIC_DECREMENT(stats->count, count);

    /* increment the shared atomic counter */
    total += RELAY_ATOMIC_INCREMENT(stats->total, count);

    stats->per_second= count; /* fix me */

    if (total_addr)
        *total_addr= total;

    return count;
}


