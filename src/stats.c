#include "stats.h"

stats_t STATS= { .packet_count= 0, .packets_per_second= 0, .accumulated= 0 };

void inc_packets() {
    RELAY_ATOMIC_INCREMENT(STATS.packet_count,1);
}

void reset_packets() {
    char str[MAX_BUF_LEN];
    stats_count_t accumulated;
    stats_count_t count;
    
    /* read the packet count */
    count= RELAY_ATOMIC_READ(STATS.packet_count);
    
    /* and now remove the amount fetched from the counter */
    RELAY_ATOMIC_DECREMENT(STATS.packet_count, count);

    /* increment the shared atomic counter */
    accumulated= RELAY_ATOMIC_INCREMENT(STATS.accumulated, count);
    /* mirror the update to our private var*/
    accumulated+= count;

    /* overwrite the packets_per_second count */
    STATS.packets_per_second= count;

    /* set it in the process name */
    snprintf(str, MAX_BUF_LEN, STATSfmt " : " STATSfmt, count, accumulated );
    setproctitle(str);
}


