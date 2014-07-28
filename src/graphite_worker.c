#include "graphite_worker.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;
extern config_t CONFIG;

void *graphite_worker_thread(void *arg) {
    struct sock *sck= NULL;
    graphite_worker_t *self = (graphite_worker_t *)arg;
    ssize_t sent_bytes;
    time_t this_epoch;

    socketize(self->arg, &self->s_output, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND);
    self->buffer= mallocz_or_die(GRAPHITE_BUFFER_MAX);

    while (!RELAY_ATOMIC_READ(self->exit)) {
        char *str= self->buffer;            /* current position in buffer */
        ssize_t len= GRAPHITE_BUFFER_MAX;   /* amount remaining to use */
        worker_t *w;

        if ( !sck ) {
            /* nope, so lets try to open one */
            if ( open_socket( &self->s_output, DO_CONNECT | DO_NOT_EXIT, 0, 0 ) ) {
                /* success, setup sck variable as a flag and save on some indirection */
                sck = &self->s_output;
            } else {
                /* no socket - wait a while, and then redo the loop */
                w_wait( CONFIG.sleep_after_disaster_ms );
                continue;
            }
        }

        /* Because of the POOL lock here we build up the full graphite send packet
         * in one buffer and send it using a single sendto() call.
         *
         * We could also use a smaller buffer and use cork() on the socket. But I
         * don't want to hold the POOL lock for the duration of the sendto() call.
         */

        LOCK(&POOL.lock);
        this_epoch= time(NULL);
        TAILQ_FOREACH(w, &POOL.workers, entries) {
            int wrote_len;
            stats_basic_counters_t totals;
            memset(&totals,0,sizeof(stats_basic_counters_t));

            snapshot_stats(&w->totals, &totals);

            if (len >= 1000) {
                wrote_len= snprintf(
                    str, len,
                    "%s.%s.received_count: "   STATSfmt " %lu\n"
                    "%s.%s.sent_count: "       STATSfmt " %lu\n"
                    "%s.%s.partial_count: "    STATSfmt " %lu\n"
                    "%s.%s.spilled_count: "    STATSfmt " %lu\n"
                    "%s.%s.error_count: "      STATSfmt " %lu\n"
                    "%s.%s.disk_count: "       STATSfmt " %lu\n"
                    "%s.%s.disk_error_count: " STATSfmt " %lu\n"
                    "%s"
                    ,
                    self->root, w->s_output.to_string, totals.received_count,  this_epoch,
                    self->root, w->s_output.to_string, totals.sent_count,      this_epoch,
                    self->root, w->s_output.to_string, totals.partial_count,   this_epoch,
                    self->root, w->s_output.to_string, totals.spilled_count,   this_epoch,
                    self->root, w->s_output.to_string, totals.error_count,     this_epoch,
                    self->root, w->s_output.to_string, totals.disk_count,      this_epoch,
                    self->root, w->s_output.to_string, totals.disk_error_count,this_epoch,
                    ""
                );

                if (wrote_len < 0 || wrote_len >= len) {
                    /* should we warn? */
                    break;
                }
                str += wrote_len;
                len -= wrote_len;
            }
        }
        UNLOCK(&POOL.lock);

        /* convert len from "amount remaining" to "amount used" */
        len = GRAPHITE_BUFFER_MAX - len;

        /* and reset the buffer pointer */
        str = self->buffer;

        /* scrub the string of unfortunate characters */
        while ( NULL != (str= strpbrk(str, "./@:~!@#$%^&*(){}[]\";<>,/?` \t\n")) )
            *str++= '_';

        /* send it */
        sent_bytes= sendto(sck->socket, self->buffer, len, 0, NULL, 0);
        if (sent_bytes != len) {
            close(sck->socket);
            sck= NULL;
        }
        w_wait(60000);
    }
    if (sck)
        close(sck->socket);

    return NULL;
}

