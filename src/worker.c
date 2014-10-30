#include "worker.h"

#include "worker_pool.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;

/* add an item to a disk worker queue */
static void enqueue_blob_for_disk_writing(worker_t * worker, struct blob *b)
{
    queue_append(&worker->disk_writer->queue, b, &POOL.lock);	/* XXX: change this to a worker level lock */
}

/* if a worker failed to send we need to write the item to the disk */
/* XXX */
static void enqueue_queue_for_disk_writing(worker_t * worker, queue_t * q)
{
    queue_append_tail(&worker->disk_writer->queue, q, &POOL.lock);	/* XXX: change this to a worker level lock */
}

static int process_queue(worker_t * self, relay_socket_t * sck, queue_t * private_queue, queue_t * spill_queue)
{
    blob_t *cur_blob;
    struct timeval now;
    struct timeval send_start_time;
    struct timeval send_end_time;
    stats_count_t spilled = 0;
    ssize_t wrote = 0;

    get_time(&send_start_time);

    cork(sck, 1);

    while (private_queue->head != NULL) {
	ssize_t bytes_sent = -2;
	ssize_t bytes_to_send = 0;

	get_time(&now);

	cur_blob = private_queue->head;

	/* Peel off all the blobs which have been in the queue
	 * for longer than the spill limit, move them to the
	 * spill queue, and enqueue them for spilling. */
	if (elapsed_usec(&BLOB_RECEIVED_TIME(cur_blob), &now) >= self->base.config->spill_usec) {
	    spill_queue->head = cur_blob;
	    spill_queue->count = 1;
	    while (BLOB_NEXT(cur_blob)
		   && elapsed_usec(&BLOB_RECEIVED_TIME(BLOB_NEXT(cur_blob)), &now) >= self->base.config->spill_usec) {
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

	cur_blob = queue_shift_nolock(private_queue);
	if (!cur_blob)
	    break;

	void *raw_bytes;
	if (sck->type == SOCK_DGRAM) {
	    bytes_to_send = BLOB_BUF_SIZE(cur_blob);
	    raw_bytes = BLOB_BUF_addr(cur_blob);
	    bytes_sent =
		sendto(sck->socket, raw_bytes,
		       bytes_to_send, MSG_NOSIGNAL, (struct sockaddr *) &sck->sa.in, sck->addrlen);
	} else {		/* sck->type == SOCK_STREAM */
	    bytes_to_send = BLOB_DATA_MBR_SIZE(cur_blob);
	    raw_bytes = BLOB_DATA_MBR_addr(cur_blob);
	    bytes_sent = sendto(sck->socket, raw_bytes, bytes_to_send, MSG_NOSIGNAL, NULL, 0);
	}
	if (0) {
	    int saverrno = errno;
	    WARN("%s: tried sending %zd bytes, sent %zd",
		 (sck->type == SOCK_DGRAM) ? "udp" : "tcp", bytes_to_send, bytes_sent);
	    void *p = raw_bytes;
	    int peek_bytes = bytes_to_send > 16 ? 16 : bytes_to_send;
	    for (int i = 0; i < peek_bytes; i++) {
		printf("%02x ", ((unsigned char *) p)[i]);
	    }
	    printf("| ");
	    for (int i = 0; i < peek_bytes; i++) {
		unsigned char c = ((unsigned char *) p)[i];
		printf("%c", isprint(c) ? c : '.');
	    }
	    if (peek_bytes < bytes_to_send)
		printf("...\n");
	    errno = saverrno;
	}

	if (bytes_sent == -1) {
	    WARN_ERRNO("sendto() tried %zd bytes to %s but wrote none", bytes_to_send, sck->to_string);
	    enqueue_blob_for_disk_writing(self, cur_blob);
	    close(sck->socket);
	    RELAY_ATOMIC_INCREMENT(self->counters.error_count, 1);
	    sck = NULL;
	    break;		/* stop sending from the hijacked queue */
	} else if (bytes_sent < bytes_to_send) {
	    WARN("sendto() tried %zd bytes to %s but wrote only %zd", bytes_sent, sck->to_string, bytes_to_send);
	    RELAY_ATOMIC_INCREMENT(self->counters.partial_count, 1);
	    wrote += bytes_sent;
	} else {
	    RELAY_ATOMIC_INCREMENT(self->counters.sent_count, 1);
	    wrote += bytes_sent;
	}
	blob_destroy(cur_blob);
    }

    cork(sck, 0);

    get_time(&send_end_time);

    if (spilled) {
	WARN("Wrote %lu items which were over spill threshold", (unsigned long) spilled);
    }

    /* this assumes end_time >= start_time */
    uint64_t usec = elapsed_usec(&send_start_time, &send_end_time);
    RELAY_ATOMIC_INCREMENT(self->counters.send_elapsed_usec, usec);

    return wrote;
}

/* create a normal relay worker thread
 * main loop for the worker process */
void *worker_thread(void *arg)
{
    worker_t *self = (worker_t *) arg;

    queue_t *main_queue = &self->queue;
    struct sock *sck = NULL;

    queue_t private_queue;
    queue_t spill_queue;

    memset(&private_queue, 0, sizeof(queue_t));
    memset(&spill_queue, 0, sizeof(queue_t));

    int join_err;

    while (!RELAY_ATOMIC_READ(self->base.stopping)) {
	/* check if we have a usable socket */
	if (!sck) {
	    /* nope, so lets try to open one */
	    if (open_socket(&self->output_socket, DO_CONNECT, 0, 0)) {
		/* success, setup sck variable as a flag and save on some indirection */
		sck = &self->output_socket;
		assert(sck->type == SOCK_DGRAM || sck->type == SOCK_STREAM);
	    } else {
		/* no socket - wait a while, and then redo the loop */
		worker_wait_millisec(self->base.config->sleep_after_disaster_millisec);
		continue;
	    }
	}
	assert(sck);

	/* if we dont have anything in our local queue we need to hijack the main one */
	if (private_queue.head == NULL) {
	    /* hijack the queue - copy the queue state into our private copy
	     * and then reset the queue state to empty. So the formerly
	     * shared queue is now private. We only do this if necessary.
	     */
	    if (!queue_hijack(main_queue, &private_queue, &POOL.lock)) {
		/* nothing to do, so sleep a while and redo the loop */
		worker_wait_millisec(self->base.config->polling_interval_millisec);
		continue;
	    }
	}

	RELAY_ATOMIC_INCREMENT(self->counters.received_count, private_queue.count);

	/* ok, so we have something in our queue to process */
	assert(private_queue.head);

	process_queue(self, sck, &private_queue, &spill_queue);

	accumulate_and_clear_stats(&self->counters, &self->recents, &self->totals);
    }

    if (control_is(RELAY_STOPPING)) {
	SAY("Stopping, trying worker flush");
	stats_count_t old_sent = self->totals.sent_count;
	ssize_t wrote = process_queue(self, sck, &private_queue, &spill_queue);
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

    /* XXX handle failure of the disk_write shutdown */
    join_err = pthread_join(self->disk_writer->base.tid, NULL);

    if (join_err)
	WARN("shutting down disk_writer thread error: %d", join_err);

    free(self->disk_writer);

    return NULL;
}


/* initialize a worker safely */
worker_t *worker_init(const char *arg, const config_t * config)
{
    worker_t *worker = calloc_or_fatal(sizeof(*worker));
    disk_writer_t *disk_writer = calloc_or_fatal(sizeof(disk_writer_t));

    if (worker == NULL || disk_writer == NULL)
	return NULL;

    int create_err;

    worker->base.config = config;
    worker->base.arg = strdup(arg);

    worker->exists = 1;

    if (!socketize(arg, &worker->output_socket, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "worker")) {
	FATAL("Failed to socketize worker");
	return NULL;
    }

    worker->disk_writer = disk_writer;

    disk_writer->base.config = config;
    disk_writer->counters = &worker->counters;
    disk_writer->recents = &worker->recents;
    disk_writer->totals = &worker->totals;

    /* setup spillway_path */
    int wrote = snprintf(disk_writer->spillway_path, PATH_MAX, "%s/event_relay.%s", config->spillway_root,
			 worker->output_socket.arg_clean);
    if (wrote < 0 || wrote >= PATH_MAX) {
	FATAL("Failed to construct spillway_path %s", disk_writer->spillway_path);
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
    create_err = pthread_create(&worker->base.tid, NULL, worker_thread, worker);
    if (create_err) {
	int join_err;

	/* we died, so shut down our "pet" disk worker, and then exit with a message */
	RELAY_ATOMIC_OR(disk_writer->base.stopping, WORKER_STOPPING);

	/* have to handle failure of the shutdown too */
	join_err = pthread_join(disk_writer->base.tid, NULL);

	if (join_err) {
	    FATAL("Failed to create socket worker, pthread error: %d, and also failed to join disk worker, pthread error: %d",
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
void worker_destroy(worker_t * worker)
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

    free(worker->base.arg);
    free(worker);
}
