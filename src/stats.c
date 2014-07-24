#include "stats.h"

void
snapshot_stats(stats_basic_counters_t *counters, stats_basic_counters_t *totals) {
    stats_count_t received_count= RELAY_ATOMIC_READ(counters->received_count);  /* number of items we have received */
    stats_count_t sent_count=     RELAY_ATOMIC_READ(counters->sent_count);      /* number of items we have sent */
    stats_count_t partial_count=  RELAY_ATOMIC_READ(counters->partial_count);   /* number of items we have spilled */
    stats_count_t spilled_count=  RELAY_ATOMIC_READ(counters->spilled_count);   /* number of items we have spilled */
    stats_count_t error_count=    RELAY_ATOMIC_READ(counters->error_count);     /* number of items that had an error */
    stats_count_t disk_count=     RELAY_ATOMIC_READ(counters->disk_count);      /* number of items written to disk */
    stats_count_t disk_error_count=  RELAY_ATOMIC_READ(counters->disk_error_count);  /* number of items we failed to write to disk */
    stats_count_t send_elapsed_usec= RELAY_ATOMIC_READ(counters->send_elapsed_usec); /* time to send */

    RELAY_ATOMIC_INCREMENT(totals->received_count, received_count);     /* number of items we have received */
    RELAY_ATOMIC_INCREMENT(totals->sent_count, sent_count);             /* number of items we have sent */
    RELAY_ATOMIC_INCREMENT(totals->partial_count, partial_count);       /* number of items we have spilled */
    RELAY_ATOMIC_INCREMENT(totals->spilled_count, spilled_count);       /* number of items we have spilled */
    RELAY_ATOMIC_INCREMENT(totals->error_count, error_count);           /* number of items that had an error */
    RELAY_ATOMIC_INCREMENT(totals->disk_count, disk_count);             /* number of items written to disk */
    RELAY_ATOMIC_INCREMENT(totals->disk_error_count, disk_error_count);   /* number of items written to disk */
    RELAY_ATOMIC_INCREMENT(totals->send_elapsed_usec, send_elapsed_usec); /* time to send */
    
    RELAY_ATOMIC_DECREMENT(counters->received_count, received_count);   /* number of items we have received */
    RELAY_ATOMIC_DECREMENT(counters->sent_count, sent_count);           /* number of items we have sent */
    RELAY_ATOMIC_DECREMENT(counters->partial_count, partial_count);     /* number of items we have spilled */
    RELAY_ATOMIC_DECREMENT(counters->spilled_count, spilled_count);     /* number of items we have spilled */
    RELAY_ATOMIC_DECREMENT(counters->error_count, error_count);         /* number of items that had an error */
    RELAY_ATOMIC_DECREMENT(counters->disk_count, disk_count);           /* number of items written to disk */
    RELAY_ATOMIC_DECREMENT(counters->disk_error_count, disk_error_count);   /* number of items written to disk */
    RELAY_ATOMIC_DECREMENT(counters->send_elapsed_usec, send_elapsed_usec); /* time to send */
}


