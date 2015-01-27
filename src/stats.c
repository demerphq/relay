#include "stats.h"

#include <math.h>
#include <string.h>

void rates_init(rates_t * rates, double decay_sec)
{
    memset(rates, 0, sizeof(rates_t));
    rates->decay_sec = decay_sec;
}

static inline void update_rate(double *rate, stats_count_t * prev, stats_count_t current, double decay, double freq)
{
    double since = (current - *prev) * freq;
    *rate = (1 - decay) * since + decay * (*rate);
    *prev = current;
}

void update_rates(rates_t * rates, const stats_basic_counters_t * totals, long since)
{
    double freq = 1.0 / since;
    double decay = exp(-since / rates->decay_sec);
    update_rate(&rates->received.rate, &rates->received.prev, totals->received_count, decay, freq);
    update_rate(&rates->sent.rate, &rates->sent.prev, totals->sent_count, decay, freq);
    update_rate(&rates->spilled.rate, &rates->spilled.prev, totals->spilled_count, decay, freq);
    update_rate(&rates->dropped.rate, &rates->dropped.prev, totals->dropped_count, decay, freq);
}

void accumulate_and_clear_stats(stats_basic_counters_t * counters, stats_basic_counters_t * recents,
                                stats_basic_counters_t * totals)
{
    stats_count_t received_count = RELAY_ATOMIC_READ(counters->received_count);
    stats_count_t sent_count = RELAY_ATOMIC_READ(counters->sent_count);
    stats_count_t partial_count = RELAY_ATOMIC_READ(counters->partial_count);
    stats_count_t spilled_count = RELAY_ATOMIC_READ(counters->spilled_count);
    stats_count_t dropped_count = RELAY_ATOMIC_READ(counters->dropped_count);
    stats_count_t error_count = RELAY_ATOMIC_READ(counters->error_count);
    stats_count_t disk_count = RELAY_ATOMIC_READ(counters->disk_count);
    stats_count_t disk_error_count = RELAY_ATOMIC_READ(counters->disk_error_count);
    stats_count_t send_elapsed_usec = RELAY_ATOMIC_READ(counters->send_elapsed_usec);

    RELAY_ATOMIC_INCREMENT(recents->received_count, received_count);
    RELAY_ATOMIC_INCREMENT(recents->sent_count, sent_count);
    RELAY_ATOMIC_INCREMENT(recents->partial_count, partial_count);
    RELAY_ATOMIC_INCREMENT(recents->spilled_count, spilled_count);
    RELAY_ATOMIC_INCREMENT(recents->dropped_count, dropped_count);
    RELAY_ATOMIC_INCREMENT(recents->error_count, error_count);
    RELAY_ATOMIC_INCREMENT(recents->disk_count, disk_count);
    RELAY_ATOMIC_INCREMENT(recents->disk_error_count, disk_error_count);
    RELAY_ATOMIC_INCREMENT(recents->send_elapsed_usec, send_elapsed_usec);

    if (totals) {
        RELAY_ATOMIC_INCREMENT(totals->received_count, received_count);
        RELAY_ATOMIC_INCREMENT(totals->sent_count, sent_count);
        RELAY_ATOMIC_INCREMENT(totals->partial_count, partial_count);
        RELAY_ATOMIC_INCREMENT(totals->spilled_count, spilled_count);
        RELAY_ATOMIC_INCREMENT(totals->dropped_count, dropped_count);
        RELAY_ATOMIC_INCREMENT(totals->error_count, error_count);
        RELAY_ATOMIC_INCREMENT(totals->disk_count, disk_count);
        RELAY_ATOMIC_INCREMENT(totals->disk_error_count, disk_error_count);
        RELAY_ATOMIC_INCREMENT(totals->send_elapsed_usec, send_elapsed_usec);
    }

    RELAY_ATOMIC_DECREMENT(counters->received_count, received_count);
    RELAY_ATOMIC_DECREMENT(counters->sent_count, sent_count);
    RELAY_ATOMIC_DECREMENT(counters->partial_count, partial_count);
    RELAY_ATOMIC_DECREMENT(counters->spilled_count, spilled_count);
    RELAY_ATOMIC_DECREMENT(counters->dropped_count, dropped_count);
    RELAY_ATOMIC_DECREMENT(counters->error_count, error_count);
    RELAY_ATOMIC_DECREMENT(counters->disk_count, disk_count);
    RELAY_ATOMIC_DECREMENT(counters->disk_error_count, disk_error_count);
    RELAY_ATOMIC_DECREMENT(counters->send_elapsed_usec, send_elapsed_usec);
}
