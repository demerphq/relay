#include "socket_worker.h"

#include <ctype.h>

#if defined(__APPLE__) || defined(__MACH__)
#include <sys/syslimits.h>
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif

#include "global.h"
#include "log.h"
#include "relay_threads.h"
#include "socket_util.h"
#include "timer.h"
#include "worker_util.h"

/* If a worker failed to send we need to write the item to the disk.
 * Or, if config has disabled spilling, the write phase will just drop them. */
static void enqueue_queue_for_disk_writing(socket_worker_t * worker, queue_t * q)
{
    queue_append_tail(&worker->disk_writer->queue, q, &worker->lock);
}

/* try to get the OS to send our packets more efficiently when sending via TCP. */
static void cork(relay_socket_t * s, int flag)
{
    if (!s || s->proto != IPPROTO_TCP)
        return;
#ifdef TCP_CORK                 /* Linux */
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK, (char *) &flag, sizeof(int)) < 0)
        WARN_ERRNO("setsockopt TCP_CORK: %s", strerror(errno));
#elif defined(TCP_NOPUSH)       /* BSD */
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_NOPUSH, (char *) &flag, sizeof(int)) < 0)
        WARN_ERRNO("setsockopt TCP_NOPUSH: %s", strerror(errno));
#else
#error No TCP_CORK or TCP_NOPUSH
#endif
}

/* Peels off all the blobs which have been in the input queue for longer
 * than the spill limit, move them to the spill queue, and enqueue
 * them for eventual spilling or dropping.
 *
 * Note that the "spill queue" is used either for actual spilling (to the disk)
 * or dropping.
 *
 * Returns the number of (eventually) spilled (if spill enabled) or
 * dropped (if spill disabled) items. */
static stats_count_t spill_by_age(socket_worker_t * self, int spill_enabled, queue_t * private_queue,
                                  queue_t * spill_queue, uint64_t spill_microsec, struct timeval *now)
{
    blob_t *cur_blob = private_queue->head;

    if (!cur_blob)
        return 0;

    /* If spill is disabled, this really counts the dropped packets. */
    stats_count_t spilled = 0;

    if (elapsed_usec(&BLOB_RECEIVED_TIME(cur_blob), now) >= spill_microsec) {
        spill_queue->head = cur_blob;
        spill_queue->count = 1;
        while (BLOB_NEXT(cur_blob)
               && elapsed_usec(&BLOB_RECEIVED_TIME(BLOB_NEXT(cur_blob)), now) >= spill_microsec) {
            cur_blob = BLOB_NEXT(cur_blob);
            spill_queue->count++;
        }
        spill_queue->tail = cur_blob;
        private_queue->head = BLOB_NEXT(cur_blob);
        private_queue->count -= spill_queue->count;
        BLOB_NEXT_set(cur_blob, NULL);

        spilled += spill_queue->count;

        if (spill_enabled) {
            RELAY_ATOMIC_INCREMENT(self->counters.spilled_count, spill_queue->count);
        } else {
            RELAY_ATOMIC_INCREMENT(self->counters.dropped_count, spill_queue->count);
        }

        enqueue_queue_for_disk_writing(self, spill_queue);
    }

    return spilled;
}

static stats_count_t spill_all(socket_worker_t * self, queue_t * private_queue, queue_t * spill_queue)
{
    blob_t *cur_blob = private_queue->head;

    if (!cur_blob)
        return 0;

    stats_count_t spilled = 0;

    spill_queue->head = cur_blob;
    spill_queue->count = 1;
    while (BLOB_NEXT(cur_blob)) {
        cur_blob = BLOB_NEXT(cur_blob);
        spill_queue->count++;
    }
    spill_queue->tail = cur_blob;
    private_queue->head = BLOB_NEXT(cur_blob);
    private_queue->count -= spill_queue->count;
    BLOB_NEXT_set(cur_blob, NULL);

    spilled += spill_queue->count;

    enqueue_queue_for_disk_writing(self, spill_queue);

    return spilled;
}

static void connected_inc()
{
    LOCK(&GLOBAL.pool.lock);
    GLOBAL.pool.n_connected++;
    SAY("Connected count %d", GLOBAL.pool.n_connected);
    UNLOCK(&GLOBAL.pool.lock);
}

static void connected_dec()
{
    LOCK(&GLOBAL.pool.lock);
    GLOBAL.pool.n_connected--;
    SAY("Connected count %d", GLOBAL.pool.n_connected);
    UNLOCK(&GLOBAL.pool.lock);
}

static int connected_all()
{
    int ret;
    LOCK(&GLOBAL.pool.lock);
    ret = GLOBAL.pool.n_connected == GLOBAL.pool.n_workers;
    UNLOCK(&GLOBAL.pool.lock);
    return ret;
}

static void peek_send(relay_socket_t * sck, const void *data, ssize_t blob_left, ssize_t sent)
{
    int saverrno = errno;
    WARN("%s: tried sending %zd bytes, sent %zd", (sck->type == SOCK_DGRAM) ? "udp" : "tcp", blob_left, sent);
    const unsigned char *p = data;
    int peek_bytes = blob_left > 16 ? 16 : blob_left;
    for (int i = 0; i < peek_bytes; i++) {
        printf("%02x ", p[i]);
    }
    printf("| ");
    for (int i = 0; i < peek_bytes; i++) {
        unsigned char c = p[i];
        printf("%c", isprint(c) ? c : '.');
    }
    if (peek_bytes < blob_left)
        printf("...\n");
    errno = saverrno;
}

static int process_queue(socket_worker_t * self, relay_socket_t * sck, queue_t * private_queue, queue_t * spill_queue,
                         ssize_t * wrote)
{
    if (sck == NULL) {
        WARN("NULL forwarding socket");
        return 0;
    }

    blob_t *cur_blob;
    struct timeval now;
    struct timeval send_start_time;
    struct timeval send_end_time;
    stats_count_t spilled = 0;

    const config_t *config = self->base.config;
    const uint64_t spill_microsec = 1000 * config->spill_millisec;
    const uint64_t grace_microsec = 1000 * config->spill_grace_millisec;

    const struct sockaddr *dest_addr = (const struct sockaddr *) &sck->sa.in;
    socklen_t addr_len = sck->addrlen;

    int in_grace_period = 0;
    struct timeval grace_period_start;

    int failed = 0;

    *wrote = 0;

    get_time(&send_start_time);

    cork(sck, 1);

    while (private_queue->head != NULL) {
        get_time(&now);

        /* While not all the socket backends are present, for a configured maximum time,
         * do not spill/drop. This is a bit crude, better rules/heuristics welcome. */
        if (!connected_all()) {
            if (in_grace_period == 0) {
                in_grace_period = 1;
                get_time(&grace_period_start);
                SAY("Spill/drop grace period of %d millisec started", config->spill_grace_millisec);
            }
            if (elapsed_usec(&grace_period_start, &now) >= grace_microsec) {
                in_grace_period = 0;
                SAY("Spill/drop grace period of %d millisec expired", config->spill_grace_millisec);
            }
        } else {
            if (in_grace_period) {
                SAY("Spill/drop grace period of %d millisec canceled", config->spill_grace_millisec);
            }
            in_grace_period = 0;
        }

        if (in_grace_period == 0) {
            spilled += spill_by_age(self, config->spill_enabled, private_queue, spill_queue, spill_microsec, &now);
        }

        cur_blob = private_queue->head;
        if (!cur_blob)
            break;

        void *blob_data;
        ssize_t blob_size;

        if (sck->type == SOCK_DGRAM) {
            blob_size = BLOB_BUF_SIZE(cur_blob);
            blob_data = BLOB_BUF_addr(cur_blob);
        } else {                /* sck->type == SOCK_STREAM */
            blob_size = BLOB_DATA_MBR_SIZE(cur_blob);
            blob_data = BLOB_DATA_MBR_addr(cur_blob);
        }

        ssize_t blob_left = blob_size;
        ssize_t blob_sent = 0;
        int sendto_errno = 0;

        failed = 0;

        /* Keep sending while we have data left since a single sendto()
         * doesn't necessarily send all of it.  This may eventually fail
         * if sendto() returns -1. */
        while (!RELAY_ATOMIC_READ(self->base.stopping) && blob_left > 0) {
            const void *data = (const char *) blob_data + blob_sent;
            ssize_t sent;

            sendto_errno = 0;
            if (sck->type == SOCK_DGRAM) {
                sent = sendto(sck->socket, data, blob_left, MSG_NOSIGNAL, dest_addr, addr_len);
            } else {            /* sck->type == SOCK_STREAM */
                sent = sendto(sck->socket, data, blob_left, MSG_NOSIGNAL, NULL, 0);
            }
            sendto_errno = errno;

            if (0) {            /* For debugging. */
                peek_send(sck, data, blob_left, sent);
            }

            if (sent == -1) {
                WARN_ERRNO("sendto() tried sending %zd bytes to %s but sent none", blob_left, sck->to_string);
                RELAY_ATOMIC_INCREMENT(self->counters.error_count, 1);
                if (sendto_errno == EINTR) {
                    /* sendto() got interrupted by a signal.  Wait a while and retry. */
                    WARN("Interrupted, resuming");
                    worker_wait_millisec(config->sleep_after_disaster_millisec);
                    continue;
                }
                failed = 1;
                break;          /* stop sending from the hijacked queue */
            }

            blob_sent += sent;
            blob_left -= sent;
        }

        if (blob_sent == blob_size) {
            RELAY_ATOMIC_INCREMENT(self->counters.sent_count, 1);
        } else if (blob_sent < blob_size) {
            /* Despite the send-loop above, we failed to send all the bytes. */
            WARN("sendto() tried sending %zd bytes to %s but sent only %zd", blob_size, sck->to_string, blob_sent);
            RELAY_ATOMIC_INCREMENT(self->counters.partial_count, 1);
            failed = 1;
        }

        *wrote += blob_sent;

        if (failed) {
            /* We failed to send this packet.  Exit the loop, and
             * right after the loop close the socket, and get out,
             * letting the main loop to reconnect. */
            if ((sendto_errno == EAGAIN || sendto_errno == EWOULDBLOCK)) {
                /* Traffic jam.  Wait a while, but still get out. */
                WARN("Traffic jam");
                worker_wait_millisec(config->sleep_after_disaster_millisec);
            }
            break;
        } else {
            queue_shift_nolock(private_queue);
            blob_destroy(cur_blob);
        }
    }

    cork(sck, 0);

    get_time(&send_end_time);

    if (spilled) {
        if (config->spill_enabled) {
            WARN("Wrote %lu items which were over spill threshold", (unsigned long) spilled);
        } else {
            WARN("Spill disabled: DROPPED %lu items which were over spill threshold", (unsigned long) spilled);
        }
    }

    /* this assumes end_time >= start_time */
    uint64_t usec = elapsed_usec(&send_start_time, &send_end_time);
    RELAY_ATOMIC_INCREMENT(self->counters.send_elapsed_usec, usec);

    return failed == 0;
}

/* the main loop for the socket worker process */
void *socket_worker_thread(void *arg)
{
    socket_worker_t *self = (socket_worker_t *) arg;

    queue_t *main_queue = &self->queue;
    relay_socket_t *sck = NULL;

    queue_t private_queue;
    queue_t spill_queue;

    memset(&private_queue, 0, sizeof(queue_t));
    memset(&spill_queue, 0, sizeof(queue_t));

    const config_t *config = self->base.config;

    int join_err;

#define RATE_UPDATE_PERIOD 15
    time_t last_rate_update = 0;

    while (!RELAY_ATOMIC_READ(self->base.stopping)) {
        time_t now = time(NULL);

        if (!sck) {
            SAY("Opening forwarding socket");
            sck = open_output_socket_eventually(&self->base);
            if (sck == NULL || !(sck->type == SOCK_DGRAM || sck->type == SOCK_STREAM)) {
                FATAL_ERRNO("Failed to open forwarding socket");
                break;
            }
            connected_inc();
        }

        long since_rate_update = now - last_rate_update;
        if (since_rate_update >= RATE_UPDATE_PERIOD) {
            last_rate_update = now;
            update_rates(&self->rates[0], &self->totals, since_rate_update);
            update_rates(&self->rates[1], &self->totals, since_rate_update);
            update_rates(&self->rates[2], &self->totals, since_rate_update);
        }

        /* if we dont have anything in our local queue we need to hijack the main one */
        if (private_queue.head == NULL) {
            /* hijack the queue - copy the queue state into our private copy
             * and then reset the queue state to empty. So the formerly
             * shared queue is now private. We only do this if necessary.
             */
            if (!queue_hijack(main_queue, &private_queue, &GLOBAL.pool.lock)) {
                /* nothing to do, so sleep a while and redo the loop */
                worker_wait_millisec(config->polling_interval_millisec);
                continue;
            }
        }

        RELAY_ATOMIC_INCREMENT(self->counters.received_count, private_queue.count);

        /* ok, so we should have something in our queue to process */
        if (private_queue.head == NULL) {
            WARN("Empty private queue");
            break;
        }

        ssize_t wrote = 0;
        if (!process_queue(self, sck, &private_queue, &spill_queue, &wrote)) {
            if (!RELAY_ATOMIC_READ(self->base.stopping)) {
                WARN("Closing forwarding socket");
                close(sck->socket);
                sck = NULL;
                connected_dec();
            }
        }

        accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
    }

    if (control_is(RELAY_STOPPING)) {
        SAY("Socket worker stopping, trying forwarding flush");
        stats_count_t old_sent = self->totals.sent_count;
        stats_count_t old_spilled = self->totals.spilled_count;
        stats_count_t old_dropped = self->totals.dropped_count;
        if (sck) {
            ssize_t wrote = 0;
            if (!process_queue(self, sck, &private_queue, &spill_queue, &wrote)) {
                WARN_ERRNO("Forwarding flush failed");
            }
            accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
            SAY("Forwarding flush forwarded %zd bytes in %llu events, spilled %llu events, dropped %llu events ",
                wrote, self->totals.sent_count - old_sent, self->totals.spilled_count - old_spilled,
                self->totals.dropped_count - old_dropped);
        } else {
            WARN("No forwarding socket to flush to");
        }
        SAY("Socket worker spilling any remaining events to disk");
        stats_count_t spilled = spill_all(self, &private_queue, &spill_queue);
        SAY("Socket worker spilled %llu events to disk", spilled);
    } else {
        accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
    }

    SAY("worker[%s] in its lifetime received %lu sent %lu spilled %lu dropped %lu",
        (sck ? sck->to_string : self->base.arg),
        (unsigned long) RELAY_ATOMIC_READ(self->totals.received_count),
        (unsigned long) RELAY_ATOMIC_READ(self->totals.sent_count),
        (unsigned long) RELAY_ATOMIC_READ(self->totals.spilled_count),
        (unsigned long) RELAY_ATOMIC_READ(self->totals.dropped_count));

    if (sck) {
        close(sck->socket);
        connected_dec();
    }

    /* we are done so shut down our "pet" disk worker, and then exit with a message */
    RELAY_ATOMIC_OR(self->disk_writer->base.stopping, WORKER_STOPPING);

    join_err = pthread_join(self->disk_writer->base.tid, NULL);

    if (join_err)
        FATAL("shutting down disk_writer thread error: pthread error %d", join_err);

    free(self->disk_writer);

    return NULL;
}


/* initialize a worker safely */
socket_worker_t *socket_worker_create(const char *arg, const config_t * config)
{
    socket_worker_t *worker = calloc_or_fatal(sizeof(*worker));
    disk_writer_t *disk_writer = calloc_or_fatal(sizeof(disk_writer_t));

    if (worker == NULL || disk_writer == NULL)
        return NULL;

    int create_err;

    worker->base.config = config;
    worker->base.arg = strdup(arg);

    worker->exists = 1;

    if (!socketize(arg, &worker->base.output_socket, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "worker")) {
        FATAL("Failed to socketize worker");
        return NULL;
    }

    worker->disk_writer = disk_writer;

    disk_writer->base.config = config;
    disk_writer->counters = &worker->counters;
    disk_writer->recents = &worker->recents;
    disk_writer->totals = &worker->totals;

#define DECAY_1MIN 60
#define DECAY_5MIN (5 * DECAY_1MIN)
#define DECAY_15MIN (15 * DECAY_1MIN)

    rates_init(&worker->rates[0], DECAY_1MIN);
    rates_init(&worker->rates[1], DECAY_5MIN);
    rates_init(&worker->rates[2], DECAY_15MIN);

    LOCK_INIT(&worker->lock);

    /* setup spill_path */
    int wrote = snprintf(disk_writer->spill_path, PATH_MAX, "%s/event_relay.%s", config->spill_root,
                         worker->base.output_socket.arg_clean);
    if (wrote < 0 || wrote >= PATH_MAX) {
        FATAL("Failed to construct spill_path %s", disk_writer->spill_path);
        return NULL;
    }

    /* Create the disk_writer before we create the main worker.
     * We do this because the disk_writer only consumes things
     * that have been handled by the main worker, and vice versa
     * when the main worker fails to send then it might want to give
     * the item to the disk worker. If we did it the other way round
     * we might have something to assign to the disk worker but no
     * disk worker to assign it to.
     */
    create_err = pthread_create(&disk_writer->base.tid, NULL, disk_writer_thread, disk_writer);
    if (create_err) {
        FATAL("Failed to create disk worker, pthread error: %d", create_err);
        return NULL;
    }

    /* and finally create the thread */
    create_err = pthread_create(&worker->base.tid, NULL, socket_worker_thread, worker);
    if (create_err) {
        int join_err;

        /* we died, so shut down our "pet" disk worker, and then exit with a message */
        RELAY_ATOMIC_OR(disk_writer->base.stopping, WORKER_STOPPING);

        /* have to handle failure of the shutdown too */
        join_err = pthread_join(disk_writer->base.tid, NULL);

        if (join_err) {
            FATAL
                ("Failed to create socket worker, pthread error: %d, and also failed to join disk worker, pthread error: %d",
                 create_err, join_err);
        } else {
            FATAL("Failed to create socket worker, pthread error: %d, disk worker shut down ok", create_err);
        }
        return NULL;
    }

    /* return the worker */
    return worker;
}

/* destroy a worker */
void socket_worker_destroy(socket_worker_t * worker)
{
    uint32_t was_stopping = RELAY_ATOMIC_OR(worker->base.stopping, WORKER_STOPPING);

    /* Avoid race between worker_pool_reload_static and worker_pool_destroy_static().
     *
     * TODO: Another possible solution for this race could be a destructor thread
     * that waits on a semaphore and then destroys all.  Possible flaw: what is
     * a thread doesn't decrement the semaphore?
     *
     * Note that similar solution is used also by the graphite worker. */
    if (was_stopping & WORKER_STOPPING)
        return;

    pthread_join(worker->base.tid, NULL);

    LOCK_DESTROY(&worker->lock);

    free(worker->base.arg);
    free(worker);
}
