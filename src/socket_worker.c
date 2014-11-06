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

/* if a worker failed to send we need to write the item to the disk */
static void enqueue_queue_for_disk_writing(socket_worker_t * worker, queue_t * q)
{
    queue_append_tail(&worker->disk_writer->queue, q, &worker->lock);
}

/* try to get the OS to send our packets more efficiently when sending via TCP. */
static void cork(relay_socket_t * s, int flag)
{
    if (!s || s->proto != IPPROTO_TCP)
	return;
#ifdef TCP_CORK			/* Linux */
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK, (char *) &flag, sizeof(int)) < 0)
	WARN_ERRNO("setsockopt TCP_CORK: %s", strerror(errno));
#elif defined(TCP_NOPUSH)	/* BSD */
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_NOPUSH, (char *) &flag, sizeof(int)) < 0)
	WARN_ERRNO("setsockopt TCP_NOPUSH: %s", strerror(errno));
#else
#error No TCP_CORK or TCP_NOPUSH
#endif
}

static int process_queue(socket_worker_t * self, relay_socket_t ** sck, queue_t * private_queue, queue_t * spill_queue)
{
    blob_t *cur_blob;
    struct timeval now;
    struct timeval send_start_time;
    struct timeval send_end_time;
    stats_count_t spilled = 0;
    ssize_t wrote = 0;

    get_time(&send_start_time);

    const uint64_t spill_microsec = 1000 * self->base.config->spill_millisec;

    const struct sockaddr *dest_addr = (const struct sockaddr *) &(*sck)->sa.in;
    socklen_t addr_len = (*sck)->addrlen;

    int failed = 0;

    cork(*sck, 1);

    while (private_queue->head != NULL) {
	get_time(&now);

	cur_blob = private_queue->head;

	/* Peel off all the blobs which have been in the queue
	 * for longer than the spill limit, move them to the
	 * spill queue, and enqueue them for spilling. */
	if (elapsed_usec(&BLOB_RECEIVED_TIME(cur_blob), &now) >= spill_microsec) {
	    spill_queue->head = cur_blob;
	    spill_queue->count = 1;
	    while (BLOB_NEXT(cur_blob)
		   && elapsed_usec(&BLOB_RECEIVED_TIME(BLOB_NEXT(cur_blob)), &now) >= spill_microsec) {
		cur_blob = BLOB_NEXT(cur_blob);
		spill_queue->count++;
	    }
	    spill_queue->tail = cur_blob;
	    private_queue->head = BLOB_NEXT(cur_blob);
	    private_queue->count -= spill_queue->count;
	    BLOB_NEXT_set(cur_blob, NULL);

	    spilled += spill_queue->count;

	    RELAY_ATOMIC_INCREMENT(self->counters.spilled_count, spill_queue->count);

	    enqueue_queue_for_disk_writing(self, spill_queue);
	}

	cur_blob = private_queue->head;
	if (!cur_blob)
	    break;

	void *blob_data;
	ssize_t blob_size;

	if ((*sck)->type == SOCK_DGRAM) {
	    blob_size = BLOB_BUF_SIZE(cur_blob);
	    blob_data = BLOB_BUF_addr(cur_blob);
	} else {		/* (*sck)->type == SOCK_STREAM */
	    blob_size = BLOB_DATA_MBR_SIZE(cur_blob);
	    blob_data = BLOB_DATA_MBR_addr(cur_blob);
	}

	ssize_t blob_left = blob_size;
	ssize_t blob_sent = 0;

	failed = 0;

	while (!RELAY_ATOMIC_READ(self->base.stopping) && blob_left > 0) {
	    const void *data = (const char *) blob_data + blob_sent;
	    ssize_t sent;

	    if ((*sck)->type == SOCK_DGRAM) {
		sent = sendto((*sck)->socket, data, blob_left, MSG_NOSIGNAL, dest_addr, addr_len);
	    } else {		/* (*sck)->type == SOCK_STREAM */
		sent = sendto((*sck)->socket, data, blob_left, MSG_NOSIGNAL, NULL, 0);
	    }

	    /* For debugging. */
	    if (0) {
		int saverrno = errno;
		WARN("%s: tried sending %zd bytes, sent %zd",
		     ((*sck)->type == SOCK_DGRAM) ? "udp" : "tcp", blob_left, sent);
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

	    if (sent == -1) {
		WARN_ERRNO("sendto() tried sending %zd bytes to %s but sent none", blob_left, (*sck)->to_string);
		RELAY_ATOMIC_INCREMENT(self->counters.error_count, 1);
		failed = 1;
		break;		/* stop sending from the hijacked queue */
	    }

	    blob_sent += sent;
	    blob_left -= sent;
	}

	if (blob_sent == blob_size) {
	    RELAY_ATOMIC_INCREMENT(self->counters.sent_count, 1);
	} else if (blob_sent < blob_size) {
	    WARN("sendto() tried sending %zd bytes to %s but sent only %zd", blob_size, (*sck)->to_string, blob_sent);
	    RELAY_ATOMIC_INCREMENT(self->counters.partial_count, 1);
	    failed = 1;
	}

	wrote += blob_sent;

	if (failed) {
	    break;
	} else {
	    queue_shift_nolock(private_queue);
	    blob_destroy(cur_blob);
	}
    }

    cork(*sck, 0);

    if (failed) {
	close((*sck)->socket);
	*sck = NULL;		/* will be reopened by the main loop */
    }

    get_time(&send_end_time);

    if (spilled) {
	WARN("Wrote %lu items which were over spill threshold", (unsigned long) spilled);
    }

    /* this assumes end_time >= start_time */
    uint64_t usec = elapsed_usec(&send_start_time, &send_end_time);
    RELAY_ATOMIC_INCREMENT(self->counters.send_elapsed_usec, usec);

    return wrote;
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

    while (!RELAY_ATOMIC_READ(self->base.stopping)) {
	if (!sck) {
	    sck = open_output_socket_eventually(&self->base);
	    if (sck == NULL || !(sck->type == SOCK_DGRAM || sck->type == SOCK_STREAM)) {
		FATAL("Failed to get socket for graphite worker");
		break;
	    }
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

	process_queue(self, &sck, &private_queue, &spill_queue);

	accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
    }

    if (control_is(RELAY_STOPPING)) {
	SAY("Stopping, trying worker flush");
	stats_count_t old_sent = self->totals.sent_count;
	ssize_t wrote = process_queue(self, &sck, &private_queue, &spill_queue);
	accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
	SAY("Worker flush wrote %zd bytes in %lu events", wrote, self->totals.sent_count - old_sent);
    } else {
	accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
    }

    SAY("worker[%s] processed %lu packets in its lifetime",
	(sck ? sck->to_string : self->base.arg), (unsigned long) RELAY_ATOMIC_READ(self->totals.received_count));

    if (sck)
	close(sck->socket);

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
