#include "graphite_worker.h"
#include "worker_pool.h"
#include <malloc.h>

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;
extern config_t CONFIG;
extern sock_t *s_listen;

void graphite_worker_destroy(graphite_worker_t *worker) {
    uint32_t old_exit= RELAY_ATOMIC_OR(worker->exit, EXIT_FLAG);

    /* why is this needed */
    if (old_exit & EXIT_FLAG)
        return;

    pthread_join(worker->tid, NULL);

    free(worker->arg);
    free(worker->root);
    free(worker->buffer);
    free(worker);
}

/* code shamelessly derived from
 * http://stackoverflow.com/questions/504810/how-do-i-find-the-current-machines-full-hostname-in-c-hostname-and-domain-info */
char *graphite_worker_setup_root(config_t *config) {
    struct addrinfo hints, *info;
    int gai_result;
    char *root;
    char *tmp, *canonname;
    int root_len;
    int wrote;

    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; /*either IPV4 or IPV6*/
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if ((gai_result = getaddrinfo(hostname, "http", &hints, &info)) != 0) {
        DIE("Failed getaddrinfo(localhost): %s\n", gai_strerror(gai_result));
    }

    if (!info)
        DIE("No info from getaddrinfo(localhost)");

    /*
    for(p = info; p != NULL; p = p->ai_next) {
        printf("hostname: %s\n", p->ai_canonname);
    }
    */


    tmp= canonname= strdup(info->ai_canonname);
    /* scrub the hostname of unfortunate characters */
    while ( NULL != (tmp= strpbrk(tmp, "./@:~!@#$%^&*(){}[]\";<>,/?` \t\n")) )
        *tmp++= '_';

    root_len= strlen(CONFIG.graphite_root)
            + strlen(canonname)
            + strlen(s_listen->arg_clean)
            + 3;                                /* two dots plus null */
    root= mallocz_or_die(root_len);
    wrote= snprintf( root, root_len, "%s.%s.%s", config->graphite_root, canonname, s_listen->arg_clean);

    if (wrote >= root_len)
        DIE("panic: failed to sprintf hostname in graphite_worker_setup_root()");
    SAY("Using '%s' as root namespace for graphite", root);
    free(canonname);
    freeaddrinfo(info);
    return root;
}


void *graphite_worker_thread(void *arg) {
    struct sock *sck= NULL;
    graphite_worker_t *self = (graphite_worker_t *)arg;
    ssize_t sent_bytes;
    time_t this_epoch;

    self->buffer= mallocz_or_die(GRAPHITE_BUFFER_MAX);
    self->arg= strdup(CONFIG.graphite_arg);
    self->root= graphite_worker_setup_root(&CONFIG);

    socketize(self->arg, &self->s_output, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND,"graphite sender");

    while (!RELAY_ATOMIC_READ(self->exit)) {
        char *str= self->buffer;            /* current position in buffer */
        ssize_t len= GRAPHITE_BUFFER_MAX;   /* amount remaining to use */
        uint32_t wait_remains;
        worker_t *w;
        struct mallinfo meminfo;
        int wrote_len;

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
            stats_basic_counters_t totals;

            memset(&totals,0,sizeof(stats_basic_counters_t));

            snapshot_stats(&w->totals, &totals);

            if (len >= 1000) {
                wrote_len= snprintf(
                    str, len,
                    "%s.%s.received_count "   STATSfmt " %lu\n"
                    "%s.%s.sent_count "       STATSfmt " %lu\n"
                    "%s.%s.partial_count "    STATSfmt " %lu\n"
                    "%s.%s.spilled_count "    STATSfmt " %lu\n"
                    "%s.%s.error_count "      STATSfmt " %lu\n"
                    "%s.%s.disk_count "       STATSfmt " %lu\n"
                    "%s.%s.disk_error_count " STATSfmt " %lu\n"
                    "%s"
                    ,
                    self->root, w->s_output.arg_clean, totals.received_count,   this_epoch,
                    self->root, w->s_output.arg_clean, totals.sent_count,       this_epoch,
                    self->root, w->s_output.arg_clean, totals.partial_count,    this_epoch,
                    self->root, w->s_output.arg_clean, totals.spilled_count,    this_epoch,
                    self->root, w->s_output.arg_clean, totals.error_count,      this_epoch,
                    self->root, w->s_output.arg_clean, totals.disk_count,       this_epoch,
                    self->root, w->s_output.arg_clean, totals.disk_error_count, this_epoch,
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

        /* get memory details */
        meminfo = mallinfo();
        wrote_len= snprintf(
            str, len,
            "%s.%s.arena %d %lu\n"
            "%s.%s.ordblks %d %lu\n"
            "%s.%s.smblks %d %lu\n"
            "%s.%s.hblks %d %lu\n"
            "%s.%s.hblkhd %d %lu\n"
            "%s.%s.usmblks %d %lu\n"
            "%s.%s.fsmblks %d %lu\n"
            "%s.%s.uordblks %d %lu\n"
            "%s.%s.fordblks %d %lu\n"
            "%s.%s.keepcost %d %lu\n"
            "%s.%s.total_from_system %d %lu\n"
            "%s.%s.total_in_use %d %lu\n"
            "%s.%s.total_free_in_process %d %lu\n"
            "%s"
            ,
            self->root, "mallinfo", meminfo.arena, this_epoch,
            self->root, "mallinfo", meminfo.ordblks, this_epoch,
            self->root, "mallinfo", meminfo.smblks, this_epoch,
            self->root, "mallinfo", meminfo.hblks, this_epoch,
            self->root, "mallinfo", meminfo.hblkhd, this_epoch,
            self->root, "mallinfo", meminfo.usmblks, this_epoch,
            self->root, "mallinfo", meminfo.fsmblks, this_epoch,
            self->root, "mallinfo", meminfo.uordblks, this_epoch,
            self->root, "mallinfo", meminfo.fordblks, this_epoch,
            self->root, "mallinfo", meminfo.keepcost, this_epoch,

            self->root, "mallinfo", meminfo.arena + meminfo.hblkhd, this_epoch,
            self->root, "mallinfo", meminfo.uordblks + meminfo.usmblks + meminfo.hblkhd, this_epoch,
            self->root, "mallinfo", meminfo.fordblks + meminfo.fsmblks, this_epoch,
            ""
        );

        if (wrote_len < 0 || wrote_len >= len) {
            /* should we warn? */
            break;
        }
        str += wrote_len;
        len -= wrote_len;

        /* convert len from "amount remaining" to "amount used" */
        len = GRAPHITE_BUFFER_MAX - len;

        /* and reset the buffer pointer */
        str = self->buffer;

        /* send it */
        /* sent_bytes= sendto(sck->socket, self->buffer, len, 0, NULL, 0); */
        sent_bytes= write(sck->socket, self->buffer, len);
        if (sent_bytes != len) {
            close(sck->socket);
            sck= NULL;
        }
        wait_remains= CONFIG.graphite_send_interval_ms;
        while ( !RELAY_ATOMIC_READ(self->exit) && ( wait_remains > 0 ) ) {
            if (wait_remains < CONFIG.graphite_sleep_poll_interval_ms) {
                w_wait(wait_remains);
                wait_remains = 0;
            } else {
                w_wait(CONFIG.graphite_sleep_poll_interval_ms);
                wait_remains -= CONFIG.graphite_sleep_poll_interval_ms;
            }
        }
    }
    if (sck)
        close(sck->socket);

    return NULL;
}

