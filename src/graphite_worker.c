void *graphite_worker_thread(void *arg) {
    struct sock *sck= NULL;
    graphite_worker_t *self = (graphite_worker_t *)arg;
    char buffer[GRAPHITE_BUFFER_MAX];
    char *str= buffer;
    ssize_t len= GRAPHITE_BUFFER_MAX;
    ssize_t sent_bytes;
    time_t this_epoch;

    socketize(self->arg, self->s_output);

    while (!RELAY_ATOMIC_READ(self->exit)) {
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
        LOCK(&POOL.lock);
        this_epoch= time(NULL);
        TAILQ_FOREACH(w, &POOL.workers, entries) {
            int wrote_len;
            stats_basic_counters_t totals;
            memset(totals,0,sizeof(stats_basic_counters));

            snapshot_stats(&w->totals, totals);

            if (len >= 1000) {
                wrote_len= snprintf(
                    str, len,
                    "%s.%s.received_count: "   STATSfmt " %d\n"
                    "%s.%s.sent_count: "       STATSfmt " %d\n"
                    "%s.%s.partial_count: "    STATSfmt " %d\n"
                    "%s.%s.spilled_count: "    STATSfmt " %d\n"
                    "%s.%s.error_count: "      STATSfmt " %d\n"
                    "%s.%s.disk_count: "       STATSfmt " %d\n"
                    ,
                    self->root, w->s_output.to_string, totals->received_count,  this_epoch,
                    self->root, w->s_output.to_string, totals->sent_count,      this_epoch,
                    self->root, w->s_output.to_string, totals->partial_count,   this_epoch,
                    self->root, w->s_output.to_string, totals->spilled_count,   this_epoch,
                    self->root, w->s_output.to_string, totals->error_count,     this_epoch,
                    self->root, w->s_output.to_string, totals->disk_count,      this_epoch
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

        len = GRAPHITE_BUFFER_MAX - len;
        str = buffer;

        /* scrub the string */
        while (str= strpbrk(str, "./@:~!@#$%^&*(){}[]\";<>,/?` \t\n"))
            *str++= '_';

        sent_bytes= sendto(sck->socket, buffer, len, 0, NULL, 0);
        if (sent_bytes != len) {
            close(sck);
            sck= NULL;
        }
        w_wait(60000);
    }
    if (sck)
        close(sck);
}

