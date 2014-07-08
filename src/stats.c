#include "stats.h"

void
snapshot_stats(stats_basic_counters_t *counters, stats_basic_counters_t *totals) {
    stats_count_t received_count= RELAY_ATOMIC_READ(counters->received_count);  /* number of items we have received */
    stats_count_t sent_count=     RELAY_ATOMIC_READ(counters->sent_count);      /* number of items we have sent */
    stats_count_t partial_count=  RELAY_ATOMIC_READ(counters->partial_count);   /* number of items we have spilled */
    stats_count_t spilled_count=  RELAY_ATOMIC_READ(counters->spilled_count);   /* number of items we have spilled */
    stats_count_t error_count=    RELAY_ATOMIC_READ(counters->error_count);     /* number of items that had an error */

    RELAY_ATOMIC_INCREMENT(totals->received_count, received_count); /* number of items we have received */
    RELAY_ATOMIC_INCREMENT(totals->sent_count, sent_count);         /* number of items we have sent */
    RELAY_ATOMIC_INCREMENT(totals->partial_count, partial_count);   /* number of items we have spilled */
    RELAY_ATOMIC_INCREMENT(totals->spilled_count, spilled_count);   /* number of items we have spilled */
    RELAY_ATOMIC_INCREMENT(totals->error_count, error_count);       /* number of items that had an error */
    
    RELAY_ATOMIC_DECREMENT(counters->received_count, received_count); /* number of items we have received */
    RELAY_ATOMIC_DECREMENT(counters->sent_count, sent_count);         /* number of items we have sent */
    RELAY_ATOMIC_DECREMENT(counters->partial_count, partial_count);   /* number of items we have spilled */
    RELAY_ATOMIC_DECREMENT(counters->spilled_count, spilled_count);   /* number of items we have spilled */
    RELAY_ATOMIC_DECREMENT(counters->error_count, error_count);       /* number of items that had an error */
}


