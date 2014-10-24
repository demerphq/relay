#include "graphite_worker.h"

#include <malloc.h>

#include "string_util.h"
#include "worker_pool.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;
extern sock_t *s_listen;

void graphite_worker_destroy(graphite_worker_t * worker)
{
    uint32_t old_exit = RELAY_ATOMIC_OR(worker->exit, EXIT_FLAG);

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
char *graphite_worker_setup_root(const config_t * config)
{
    struct addrinfo hints, *info;
    int gai_result;
    char *root;
    int root_len;
    int wrote;

    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;	/*either IPV4 or IPV6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if ((gai_result = getaddrinfo(hostname, "http", &hints, &info)) != 0) {
	DIE("Failed getaddrinfo(localhost): %s\n", gai_strerror(gai_result));
    }

    if (!info)
	DIE("No info from getaddrinfo(localhost)");

    scrub_nonalnum(hostname, sizeof(hostname));

    root_len = strlen(config->graphite.target) + strlen(hostname) + strlen(s_listen->arg_clean) + 3;	/* two dots plus null */
    root = calloc_or_die(root_len);
    wrote = snprintf(root, root_len, "%s.%s.%s", config->graphite.target, hostname, s_listen->arg_clean);

    if (wrote >= root_len)
	DIE("panic: failed to snprintf hostname in graphite_worker_setup_root()");
    SAY("Using '%s' as root namespace for graphite", root);
    freeaddrinfo(info);
    return root;
}


graphite_worker_t *graphite_worker_create(const config_t * config)
{
    graphite_worker_t *worker = calloc_or_die(sizeof(graphite_worker_t));

    worker->config = config;
    worker->buffer = calloc_or_die(GRAPHITE_BUFFER_MAX);
    worker->arg = strdup(config->graphite.addr);
    worker->root = graphite_worker_setup_root(config);

    socketize(worker->arg, &worker->s_output, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "graphite sender");

    return worker;
}

void *graphite_worker_thread(void *arg)
{
    struct sock *sck = NULL;
    graphite_worker_t *self = (graphite_worker_t *) arg;
    ssize_t sent_bytes;
    time_t this_epoch;
    char stats_format[256];
    char meminfo_format[256];

    while (!RELAY_ATOMIC_READ(self->exit)) {
	char *str = self->buffer;	/* current position in buffer */
	ssize_t len = GRAPHITE_BUFFER_MAX;	/* amount remaining to use */
	uint32_t wait_remains_millisec;
	worker_t *w;
	struct mallinfo meminfo;
	int wrote_len;

	if (!sck) {
	    /* nope, so lets try to open one */
	    if (open_socket(&self->s_output, DO_CONNECT | DO_NOT_EXIT, 0, 0)) {
		/* success, setup sck variable as a flag and save on some indirection */
		sck = &self->s_output;
	    } else {
		/* no socket - wait a while, and then redo the loop */
		worker_wait_millisec(self->config->sleep_after_disaster_millisec);
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
	this_epoch = time(NULL);
	TAILQ_FOREACH(w, &POOL.workers, entries) {
	    stats_basic_counters_t totals;

	    memset(&totals, 0, sizeof(stats_basic_counters_t));

	    accumulate_and_clear_stats(&w->totals, &totals);

	    snprintf(stats_format, sizeof(stats_format), "%s.%s.%%s %%d %lu\n", self->root, w->s_output.arg_clean,
		     this_epoch);

	    {
		int i;
		for (i = 0;; i++) {
		    const char *label = NULL;
		    uint64_t value = 0;
		    switch (i) {
#define STATS_LABEL(name) label = #name; value = totals.name##_count
		    case 0:
			STATS_LABEL(received);
			break;
		    case 1:
			STATS_LABEL(sent);
			break;
		    case 2:
			STATS_LABEL(partial);
			break;
		    case 3:
			STATS_LABEL(spilled);
			break;
		    case 4:
			STATS_LABEL(error);
			break;
		    case 5:
			STATS_LABEL(disk);
			break;
		    case 6:
			STATS_LABEL(disk_error);
			break;
		    default:
			break;
		    }
		    if (label == NULL)
			break;
		    wrote_len = snprintf(str, len, stats_format, label, value);
		    if (wrote_len < 0 || wrote_len >= len) {
			/* should we warn? */
			break;
		    }
		    if (len > GRAPHITE_BUFFER_MAX)
			break;
		    str += wrote_len;
		    len -= wrote_len;
		}
	    }
	}
	UNLOCK(&POOL.lock);

	/* get memory details */
	meminfo = mallinfo();

	/* No need to keep reformatting root, "mallinfo", and epoch. */
	snprintf(meminfo_format, sizeof(meminfo_format), "%s.mallinfo.%%s %%d %lu\n", self->root, this_epoch);

	{
	    int i;
	    for (i = 0;; i++) {
		const char *label = NULL;
		int value = -1;
		switch (i) {
#define MEMINFO_LABEL(name) label = #name; value = meminfo.name
		case 0:
		    MEMINFO_LABEL(arena);
		    break;
		case 1:
		    MEMINFO_LABEL(ordblks);
		    break;
		case 2:
		    MEMINFO_LABEL(smblks);
		    break;
		case 3:
		    MEMINFO_LABEL(hblks);
		    break;
		case 4:
		    MEMINFO_LABEL(hblkhd);
		    break;
		case 5:
		    MEMINFO_LABEL(usmblks);
		    break;
		case 6:
		    MEMINFO_LABEL(fsmblks);
		    break;
		case 7:
		    MEMINFO_LABEL(uordblks);
		    break;
		case 8:
		    MEMINFO_LABEL(fordblks);
		    break;
		case 9:
		    MEMINFO_LABEL(keepcost);
		    break;
		case 10:
		    label = "total_from_system";
		    value = meminfo.arena + meminfo.hblkhd;
		    break;
		case 11:
		    label = "total_in_use";
		    value = meminfo.uordblks + meminfo.usmblks + meminfo.hblkhd;
		    break;
		case 12:
		    label = "total_free_in_process";
		    value = meminfo.fordblks + meminfo.fsmblks;
		    break;
		default:
		    break;
		}
		if (label == NULL)
		    break;
		wrote_len = snprintf(str, len, meminfo_format, label, value);
		if (wrote_len < 0 || wrote_len >= len) {
		    /* should we warn? */
		    break;
		}
		if (len > GRAPHITE_BUFFER_MAX)
		    break;
		str += wrote_len;
		len -= wrote_len;
	    }
	}

	/* convert len from "amount remaining" to "amount used" */
	len = GRAPHITE_BUFFER_MAX - len;

	/* and reset the buffer pointer */
	str = self->buffer;

	/* send it */
	/* sent_bytes= sendto(sck->socket, self->buffer, len, 0, NULL, 0); */
	sent_bytes = write(sck->socket, self->buffer, len);
	if (sent_bytes != len) {
	    close(sck->socket);
	    sck = NULL;
	}
	wait_remains_millisec = self->config->graphite.send_interval_millisec;
	while (!RELAY_ATOMIC_READ(self->exit) && (wait_remains_millisec > 0)) {
	    if (wait_remains_millisec < self->config->graphite.sleep_poll_interval_millisec) {
		worker_wait_millisec(wait_remains_millisec);
		wait_remains_millisec = 0;
	    } else {
		worker_wait_millisec(self->config->graphite.sleep_poll_interval_millisec);
		wait_remains_millisec -= self->config->graphite.sleep_poll_interval_millisec;
	    }
	}
    }
    if (sck)
	close(sck->socket);

    return NULL;
}
