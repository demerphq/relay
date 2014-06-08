#include "stats.h"

struct __stats {
    volatile stats_count_t   sent_count;
    volatile stats_count_t   sent_total;
    volatile stats_count_t   sent_per_second;

    volatile stats_count_t   received_count;
    volatile stats_count_t   received_total;
    volatile stats_count_t   received_per_second;
};
typedef struct __stats stats_t;

stats_t STATS= {
    .sent_count= 0,
    .sent_total= 0,
    .sent_per_second= 0,

    .received_count= 0,
    .received_total= 0,
    .received_per_second= 0
};

void inc_received_count() {
    RELAY_ATOMIC_INCREMENT(STATS.received_count,1);
}

void inc_sent_count() {
    RELAY_ATOMIC_INCREMENT(STATS.sent_count,1);
}

void mark_second_elapsed() {
    char str[MAX_BUF_LEN];
    stats_count_t sent= RELAY_ATOMIC_READ(STATS.sent_count);
    stats_count_t received= RELAY_ATOMIC_READ(STATS.received_count);
    stats_count_t sent_total= sent;
    stats_count_t received_total= received;
    
    /* and now remove the amount fetched from the counter */
    RELAY_ATOMIC_DECREMENT(STATS.sent_count, sent);
    RELAY_ATOMIC_DECREMENT(STATS.received_count, received);

    /* increment the shared atomic counter */
    sent_total += RELAY_ATOMIC_INCREMENT(STATS.sent_total, sent);
    received_total += RELAY_ATOMIC_INCREMENT(STATS.received_total, received);

    STATS.sent_per_second= sent;
    STATS.received_per_second= received;

    /* set it in the process name */
    snprintf(
        str, MAX_BUF_LEN,
        STATSfmt " : " STATSfmt " - " STATSfmt " : " STATSfmt,
        received, sent, received_total, sent_total  );
    setproctitle(str);
}


