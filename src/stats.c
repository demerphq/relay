#include "stats.h"

#include <math.h>
#include <string.h>

void rates_init(rates_t * rates, double decay_sec)
{
    memset(rates, 0, sizeof(rates_t));
    rates->decay_sec = decay_sec;
}

void update_rates(rates_t * rates, const stats_basic_counters_t * totals, long since)
{
    double freq = 1.0 / since;
    double decay = exp(-since / rates->decay_sec);
    double received_since = (totals->received_count - rates->received.prev) * freq;
    double sent_since = (totals->sent_count - rates->sent.prev) * freq;
    double spilled_since = (totals->spilled_count - rates->spilled.prev) * freq;
    rates->received.rate = (1.0 - decay) * received_since + decay * rates->received.rate;
    rates->sent.rate = (1.0 - decay) * sent_since + decay * rates->sent.rate;
    rates->spilled.rate = (1.0 - decay) * spilled_since + decay * rates->spilled.rate;
    rates->received.prev = totals->received_count;
    rates->sent.prev = totals->sent_count;
    rates->spilled.prev = totals->spilled_count;
}

void accumulate_and_clear_stats(stats_basic_counters_t * counters, stats_basic_counters_t * recents,
				stats_basic_counters_t * totals)
{
    stats_count_t received_count = RELAY_ATOMIC_READ(counters->received_count);
    stats_count_t sent_count = RELAY_ATOMIC_READ(counters->sent_count);
    stats_count_t partial_count = RELAY_ATOMIC_READ(counters->partial_count);
    stats_count_t spilled_count = RELAY_ATOMIC_READ(counters->spilled_count);
    stats_count_t error_count = RELAY_ATOMIC_READ(counters->error_count);
    stats_count_t disk_count = RELAY_ATOMIC_READ(counters->disk_count);
    stats_count_t disk_error_count = RELAY_ATOMIC_READ(counters->disk_error_count);
    stats_count_t send_elapsed_usec = RELAY_ATOMIC_READ(counters->send_elapsed_usec);

    RELAY_ATOMIC_INCREMENT(recents->received_count, received_count);
    RELAY_ATOMIC_INCREMENT(recents->sent_count, sent_count);
    RELAY_ATOMIC_INCREMENT(recents->partial_count, partial_count);
    RELAY_ATOMIC_INCREMENT(recents->spilled_count, spilled_count);
    RELAY_ATOMIC_INCREMENT(recents->error_count, error_count);
    RELAY_ATOMIC_INCREMENT(recents->disk_count, disk_count);
    RELAY_ATOMIC_INCREMENT(recents->disk_error_count, disk_error_count);
    RELAY_ATOMIC_INCREMENT(recents->send_elapsed_usec, send_elapsed_usec);

    if (totals) {
	RELAY_ATOMIC_INCREMENT(totals->received_count, received_count);
	RELAY_ATOMIC_INCREMENT(totals->sent_count, sent_count);
	RELAY_ATOMIC_INCREMENT(totals->partial_count, partial_count);
	RELAY_ATOMIC_INCREMENT(totals->spilled_count, spilled_count);
	RELAY_ATOMIC_INCREMENT(totals->error_count, error_count);
	RELAY_ATOMIC_INCREMENT(totals->disk_count, disk_count);
	RELAY_ATOMIC_INCREMENT(totals->disk_error_count, disk_error_count);
	RELAY_ATOMIC_INCREMENT(totals->send_elapsed_usec, send_elapsed_usec);
    }

    RELAY_ATOMIC_DECREMENT(counters->received_count, received_count);
    RELAY_ATOMIC_DECREMENT(counters->sent_count, sent_count);
    RELAY_ATOMIC_DECREMENT(counters->partial_count, partial_count);
    RELAY_ATOMIC_DECREMENT(counters->spilled_count, spilled_count);
    RELAY_ATOMIC_DECREMENT(counters->error_count, error_count);
    RELAY_ATOMIC_DECREMENT(counters->disk_count, disk_count);
    RELAY_ATOMIC_DECREMENT(counters->disk_error_count, disk_error_count);
    RELAY_ATOMIC_DECREMENT(counters->send_elapsed_usec, send_elapsed_usec);
}
