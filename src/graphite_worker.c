#include "graphite_worker.h"

#ifdef HAVE_MALLINFO
#include <malloc.h>
#endif

#include "global.h"
#include "log.h"
#include "relay_threads.h"
#include "socket_worker_pool.h"
#include "string_util.h"
#include "worker_util.h"

#define FORMAT_BUFFER_SIZE 256

void graphite_worker_destroy(graphite_worker_t * worker)
{
    uint32_t was_stopping = RELAY_ATOMIC_OR(worker->base.stopping, WORKER_STOPPING);

    /* Avoid race between worker_pool_reload_static and worker_pool_destroy_static(). */
    if (was_stopping & WORKER_STOPPING)
        return;

    pthread_join(worker->base.tid, NULL);

    free(worker->base.arg);
    fixed_buffer_destroy(worker->path_root);
    fixed_buffer_destroy(worker->send_buffer);
    free(worker);
}

fixed_buffer_t *graphite_worker_setup_root(graphite_worker_t * worker, const config_t * config)
{
    if (config == NULL) {
        FATAL("NULL config");
        return NULL;
    }
    if (GLOBAL.listener == NULL) {
        FATAL("NULL listener");
        return NULL;
    }
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif
    char hostname[HOST_NAME_MAX];
    hostname[HOST_NAME_MAX - 1] = 0;
    if (gethostname(hostname, sizeof(hostname) - 1)) {
        FATAL_ERRNO("gethostname failed");
        return NULL;
    }

    reverse_dotwise(hostname);

    fixed_buffer_t *root = fixed_buffer_create(256);

    int failed = 0;
    if (fixed_buffer_vcatf(root, "%s.%s", config->graphite.path_root, hostname)) {
        if (config->graphite.add_ports) {
            if (!fixed_buffer_vcatf(root, ".%s.%s", GLOBAL.listener->arg_clean, worker->base.output_socket.arg_clean)) {
                failed = 1;
            }
        }
    } else {
        failed = 1;
    }

    if (failed) {
        FATAL("Failed to add hostname");
        fixed_buffer_destroy(root);
        root = NULL;
    } else {
        SAY("Using '%s' as root namespace for graphite", root->data);
    }

    return root;
}


graphite_worker_t *graphite_worker_create(const config_t * config)
{
    graphite_worker_t *worker = calloc_or_fatal(sizeof(graphite_worker_t));

    if (worker == NULL)
        return NULL;

    worker->base.config = config;
    worker->base.arg = strdup(config->graphite.dest_addr);

    worker->send_buffer = fixed_buffer_create(GRAPHITE_BUFFER_MAX);

    if (!socketize(worker->base.arg, &worker->base.output_socket, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND,
                   "graphite worker")) {
        FATAL("Failed to socketize graphite worker");
    }

    worker->path_root = graphite_worker_setup_root(worker, config);

    return worker;
}

static int graphite_build_worker(graphite_worker_t * self, socket_worker_t * w, fixed_buffer_t * buffer,
                                 time_t this_epoch, char *stats_format)
{
    stats_basic_counters_t recents;

    memset(&recents, 0, sizeof(stats_basic_counters_t));

    accumulate_and_clear_stats(&w->recents, &recents, NULL);

    int wrote = snprintf(stats_format, FORMAT_BUFFER_SIZE, "%s.%%s %%ld %lu\n", self->path_root->data,
                         this_epoch);
    if (wrote < 0 || wrote >= FORMAT_BUFFER_SIZE) {
        WARN("Failed to initialize stats format: %s", stats_format);
        return 0;
    }

    do {
#define STATS_VCATF(name) \
	if (!fixed_buffer_vcatf(buffer, stats_format, #name, recents.name##_count)) return 0
        STATS_VCATF(received);
        STATS_VCATF(sent);
        STATS_VCATF(partial);
        STATS_VCATF(spilled);
        STATS_VCATF(dropped);
        STATS_VCATF(error);
        STATS_VCATF(disk);
        STATS_VCATF(disk_error);
    } while (0);
    if (buffer->used >= buffer->size)
        return 0;
    return 1;
}

static int graphite_build(graphite_worker_t * self, fixed_buffer_t * buffer, time_t this_epoch,
                          char *stats_format, char *meminfo_format
#ifndef HAS_MALLINFO
                          __attribute__ ((unused))
#endif
    )
{
    /* Because of the POOL lock here we build up the full graphite send packet
     * in one buffer and send it using a single write() call.
     *
     * We could also use a smaller buffer and use cork() on the socket. But I
     * don't want to hold the POOL lock for the duration of the sendto() call.
     */

    LOCK(&GLOBAL.pool.lock);

    fixed_buffer_reset(buffer);

    socket_worker_t *w;
    TAILQ_FOREACH(w, &GLOBAL.pool.workers, entries) {
        if (!graphite_build_worker(self, w, buffer, this_epoch, stats_format)) {
            WARN("Failed to build graphite buffer");
            break;
        }
    }
    UNLOCK(&GLOBAL.pool.lock);

#ifdef HAVE_MALLINFO
    /* get memory details */
    struct mallinfo meminfo = mallinfo();

    /* No need to keep reformatting root, "mallinfo", and epoch. */
    int wrote =
        snprintf(meminfo_format, FORMAT_BUFFER_SIZE, "%s.mallinfo.%%s %%d %lu\n", self->path_root->data, this_epoch);
    if (wrote < 0 || wrote >= FORMAT_BUFFER_SIZE) {
        WARN("Failed to initialize meminfo format: %s", stats_format);
        return 0;
    }

    do {
#define MEMINFO_VCATF_LABEL_VALUE(label, value) \
	if (!fixed_buffer_vcatf(buffer, meminfo_format, label, value)) return 0
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
#endif                          /* #ifdef HAVE_MALLINFO */

#ifdef HAVE_PROC_SELF_STATM
    {
        FILE *statm = fopen("/proc/self/statm", "r");
        if (statm) {
            const config_t *config = self->base.config;
            int found = 0;
            char buf[128];
            if (fgets(buf, sizeof(buf), statm)) {
                char *s = buf, *endp;
                long size = -1, rss = -1;
                size = strtol(s, &endp, 10);
                if (*endp == ' ') {
                    s = endp + 1;
                    rss = strtol(s, &endp, 10);
                }
                if (size > 0 && rss > 0) {
                    size *= config->pagesize / 1024;
                    rss *= config->pagesize / 1024;
                    fixed_buffer_vcatf(buffer, "%s.statm.size %ld %lu\n", self->path_root->data, size, this_epoch);
                    fixed_buffer_vcatf(buffer, "%s.statm.rss %ld %lu\n", self->path_root->data, rss, this_epoch);
                    found = 1;
                }
            }
            if (!found) {
                WARN("Failed to find size and rss");
            }
            fclose(statm);
        } else {
            WARN_ERRNO("Failed to open /proc/self/statm");
        }
    }
#endif

    return 1;
}

static int graphite_send(relay_socket_t * sck, fixed_buffer_t * buffer, ssize_t * wrote)
{
    /* sendto(sck->socket, buffer->data, buffer->used, 0, NULL, 0); */
    *wrote = write(sck->socket, buffer->data, buffer->used);
    return *wrote == buffer->used;
}

static void graphite_wait(graphite_worker_t * self, const struct graphite_config *graphite)
{
    uint32_t wait_remains_millisec = graphite->send_interval_millisec;
    /* We do this because a graphite worker might sleep for a while, for instance
     * 60 seconds, between sends. But in shutdown we don't want to wait that
     * long. So we sleep in chunks, and use two config vars: send_interval_millisec
     * which controls how long in total we sleep between sends, and
     * sleep_poll_interval_millisec, which determines how long each sleep "chunk" is.
     */

    while (!RELAY_ATOMIC_READ(self->base.stopping) && (wait_remains_millisec > 0)) {
        if (wait_remains_millisec < graphite->sleep_poll_interval_millisec) {
            worker_wait_millisec(wait_remains_millisec);
            wait_remains_millisec = 0;
        } else {
            worker_wait_millisec(graphite->sleep_poll_interval_millisec);
            wait_remains_millisec -= graphite->sleep_poll_interval_millisec;
        }
    }
}

void *graphite_worker_thread(void *arg)
{
    // blocking all signals in threads is a good practise
    // we let main thread receive all signals
    sigset_t sigs_to_block;
    sigfillset(&sigs_to_block);
    pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);

    relay_socket_t *sck = NULL;
    graphite_worker_t *self = (graphite_worker_t *) arg;
    char stats_format[256];
#ifdef HAVE_MALLINFO
    char meminfo_format[256];
    char *meminfo = meminfo_format;
#else
    char *meminfo = NULL;
#endif

    const config_t *config = self->base.config;
    const struct graphite_config *graphite = &config->graphite;
    fixed_buffer_t *buffer = self->send_buffer;
    ssize_t wrote = 0;

    while (!RELAY_ATOMIC_READ(self->base.stopping)) {
        if (!graphite_build(self, buffer, time(NULL), stats_format, meminfo)) {
            WARN("Failed graphite build");
            break;
        }

        if (!sck) {
            sck = open_output_socket_eventually(&self->base);
            if (sck == NULL) {
                FATAL("Failed to get socket for graphite");
                break;
            }
        }

        if (!graphite_send(sck, buffer, &wrote)) {
            WARN_ERRNO("Failed graphite send: tried %zd, wrote %zd bytes", buffer->used, wrote);
            close(sck->socket);
            sck = NULL;
            continue;
        }

        graphite_wait(self, graphite);
    }

    if (sck) {
        if (control_is(RELAY_STOPPING)) {
            /* Try to flush. */
            SAY("Graphite worker stopping, trying graphite flush");
            if (graphite_build(self, buffer, time(NULL), stats_format, meminfo)) {
                if (graphite_send(sck, buffer, &wrote)) {
                    SAY("Graphite flush successful, wrote %zd bytes", wrote);
                } else {
                    WARN("Failed graphite send: tried %zd, wrote %zd bytes", buffer->used, wrote);
                }
            } else {
                WARN("Failed graphite send: tried %zd, wrote %zd bytes", buffer->used, wrote);
            }
        }
        close(sck->socket);
    } else {
        WARN("No graphite socket, not flushing");
    }

    if (control_is_not(RELAY_STOPPING)) {
        FATAL("graphite worker died");
    }

    return NULL;
}
