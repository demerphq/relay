#include "graphite_worker.h"

#ifdef HAVE_MALLINFO
#include <malloc.h>
#endif

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
    fixed_buffer_destroy(worker->buffer);
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

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;	/*either IPV4 or IPV6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if ((gai_result = getaddrinfo("localhost", "http", &hints, &info)) != 0) {
	DIE("Failed getaddrinfo(localhost): %s\n", gai_strerror(gai_result));
    }

    if (!info)
	DIE("No info from getaddrinfo(localhost)");

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif
    char hostname[HOST_NAME_MAX];
    hostname[HOST_NAME_MAX - 1] = 0;
    gethostname(hostname, sizeof(hostname) - 1);

    scrub_nonalnum(hostname, sizeof(hostname));

    root_len = strlen(config->graphite.target) + strlen(hostname) + strlen(s_listen->arg_clean) + 3;	/* two dots plus null */
    root = calloc_or_die(root_len);
    wrote = snprintf(root, root_len, "%s.%s.%s", config->graphite.target, hostname, s_listen->arg_clean);

    if (wrote < 0 || wrote >= root_len)
	DIE("panic: failed to snprintf hostname in graphite_worker_setup_root()");
    SAY("Using '%s' as root namespace for graphite", root);
    freeaddrinfo(info);
    return root;
}


graphite_worker_t *graphite_worker_create(const config_t * config)
{
    graphite_worker_t *worker = calloc_or_die(sizeof(graphite_worker_t));

    worker->config = config;
    worker->buffer = fixed_buffer_create(GRAPHITE_BUFFER_MAX);
    worker->arg = strdup(config->graphite.addr);
    worker->root = graphite_worker_setup_root(config);

    if (!socketize(worker->arg, &worker->s_output, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "graphite worker"))
	DIE_RC(EXIT_FAILURE, "Failed to socketize graphite worker");

    return worker;
}

void *graphite_worker_thread(void *arg)
{
    struct sock *sck = NULL;
    graphite_worker_t *self = (graphite_worker_t *) arg;
    time_t this_epoch;
    char stats_format[256];
#ifdef HAVE_MALLINFO
    char meminfo_format[256];
#endif

    fixed_buffer_t *buffer = self->buffer;
    while (!RELAY_ATOMIC_READ(self->exit)) {
	uint32_t wait_remains_millisec;
	worker_t *w;
#ifdef HAVE_MALLINFO
	struct mallinfo meminfo;
#endif
	ssize_t wrote;

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

	/* reset the buffer to the beginning */
	buffer->used = 0;

	this_epoch = time(NULL);
	TAILQ_FOREACH(w, &POOL.workers, entries) {
	    stats_basic_counters_t totals;

	    memset(&totals, 0, sizeof(stats_basic_counters_t));

	    accumulate_and_clear_stats(&w->totals, &totals);

	    wrote =
		snprintf(stats_format, sizeof(stats_format), "%s.%s.%%s %%d %lu\n", self->root, w->s_output.arg_clean,
			 this_epoch);
	    if (wrote < 0 || wrote >= (int) sizeof(stats_format)) {
		WARN("Failed to initialize stats format: %s", stats_format);
		break;
	    }

	    do {
#define STATS_VCATF(name) \
		if (!fixed_buffer_vcatf(buffer, stats_format, #name, totals.name##_count)) break
		STATS_VCATF(received);
		STATS_VCATF(sent);
		STATS_VCATF(partial);
		STATS_VCATF(spilled);
		STATS_VCATF(error);
		STATS_VCATF(disk);
		STATS_VCATF(disk_error);
	    } while (0);
	    if (buffer->used >= buffer->size)
		break;
	}
	UNLOCK(&POOL.lock);

#ifdef HAVE_MALLINFO
	/* get memory details */
	meminfo = mallinfo();

	/* No need to keep reformatting root, "mallinfo", and epoch. */
	wrote = snprintf(meminfo_format, sizeof(meminfo_format), "%s.mallinfo.%%s %%d %lu\n", self->root, this_epoch);
	if (wrote < 0 || wrote >= (int) sizeof(stats_format)) {
	    WARN("Failed to initialize meminfo format: %s", stats_format);
	    break;
	}

	do {
#define MEMINFO_VCATF_LABEL_VALUE(label, value) \
	    if (!fixed_buffer_vcatf(buffer, meminfo_format, label, value)) break
#define MEMINFO_VCATF(name) MEMINFO_VCATF_LABEL_VALUE(#name, meminfo.name)
	    MEMINFO_VCATF(arena);
	    MEMINFO_VCATF(ordblks);
	    MEMINFO_VCATF(smblks);
	    MEMINFO_VCATF(hblks);
	    MEMINFO_VCATF(hblkhd);
	    MEMINFO_VCATF(usmblks);
	    MEMINFO_VCATF(fsmblks);
	    MEMINFO_VCATF(uordblks);
	    MEMINFO_VCATF(fordblks);
	    MEMINFO_VCATF(keepcost);
	    MEMINFO_VCATF_LABEL_VALUE("total_from_system", meminfo.arena + meminfo.hblkhd);
	    MEMINFO_VCATF_LABEL_VALUE("total_in_use", meminfo.uordblks + meminfo.usmblks + meminfo.hblkhd);
	    MEMINFO_VCATF_LABEL_VALUE("total_free_in_process", meminfo.fordblks + meminfo.fsmblks);
	} while (0);
#endif

	/* send it */
	/* wrote = sendto(sck->socket, buffer->data, buffer->used, 0, NULL, 0); */
	wrote = write(sck->socket, buffer->data, buffer->used);
	if (wrote != buffer->used) {
	    WARN("Failed graphite send: tried %ld, wrote %ld", buffer->used, wrote);
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
