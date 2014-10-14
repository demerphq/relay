#include "worker.h"
#include "worker_pool.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;
extern config_t CONFIG;

/* add an item to a disk worker queue */
static void enqueue_blob_for_disk_writing(worker_t * worker, struct blob *b)
{
    q_append(&worker->disk_writer->queue, b, &POOL.lock);	/* XXX: change this to a worker level lock */
}

/* if a worker failed to send we need to write the item to the disk */
/* XXX */
static void enqueue_queue_for_disk_writing(worker_t * worker, queue_t * q)
{
    q_append_q(&worker->disk_writer->queue, q, &POOL.lock);	/* XXX: change this to a worker level lock */
}

/* create a normal relay worker thread
 * main loop for the worker process */
void *worker_thread(void *arg)
{
    worker_t *self = (worker_t *) arg;

    queue_t private_queue;
    queue_t spill_queue;

    queue_t *main_queue = &self->queue;
    struct sock *sck = NULL;

    blob_t *cur_blob;
    int join_err;

    memset(&private_queue, 0, sizeof(private_queue));
    memset(&spill_queue, 0, sizeof(spill_queue));

    while (!RELAY_ATOMIC_READ(self->exit)) {
	struct timeval send_start_time;
	struct timeval send_end_time;
	struct timeval now;
	uint64_t usec;
	stats_count_t spilled = 0;

	/* check if we have a usable socket */
	if (!sck) {
	    /* nope, so lets try to open one */
	    if (open_socket(&self->s_output, DO_CONNECT | DO_NOT_EXIT, 0, 0)) {
		/* success, setup sck variable as a flag and save on some indirection */
		sck = &self->s_output;
	    } else {
		/* no socket - wait a while, and then redo the loop */
		worker_wait(CONFIG.sleep_after_disaster_ms);
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
	    if (!q_hijack(main_queue, &private_queue, &POOL.lock)) {
		/* nothing to do, so sleep a while and redo the loop */
		worker_wait(CONFIG.polling_interval_ms);
		continue;
	    }
	} else {
	    RELAY_ATOMIC_INCREMENT(self->counters.received_count, private_queue.count);
	}

	/* ok, so we have something in our queue to process */
	assert(private_queue.head);

	get_time(&send_start_time);

	cork(sck, 1);
	while (private_queue.head != NULL) {
	    ssize_t bytes_sent = -2;
	    ssize_t bytes_to_send = 0;

	    get_time(&now);

	    cur_blob = private_queue.head;

	    /* Peel off all the blobs which have been in the queue
	     * for longer than the spill limit, move them to the
	     * spill queue, and enqueue them for spilling. */
	    if (elapsed_usec(&BLOB_RECEIVED_TIME(cur_blob), &now) >= CONFIG.spill_usec) {
		spill_queue.head = cur_blob;
		spill_queue.count = 1;
		while (BLOB_NEXT(cur_blob)
		       && elapsed_usec(&BLOB_RECEIVED_TIME(BLOB_NEXT(cur_blob)), &now) >= CONFIG.spill_usec) {
		    cur_blob = BLOB_NEXT(cur_blob);
		    spill_queue.count++;
		}
		spill_queue.tail = cur_blob;
		private_queue.head = BLOB_NEXT(cur_blob);
		private_queue.count -= spill_queue.count;
		BLOB_NEXT_set(cur_blob, NULL);

		spilled += spill_queue.count;

		RELAY_ATOMIC_INCREMENT(self->counters.spilled_count, spill_queue.count);

		enqueue_queue_for_disk_writing(self, &spill_queue);
	    }

	    cur_blob = q_shift_nolock(&private_queue);
	    if (!cur_blob)
		break;

	    if (sck->type == SOCK_DGRAM) {
		bytes_to_send = BLOB_BUF_SIZE(cur_blob);
		bytes_sent =
		    sendto(sck->socket, BLOB_BUF_addr(cur_blob),
			   bytes_to_send, MSG_NOSIGNAL, (struct sockaddr *) &sck->sa.in, sck->addrlen);
	    } else {
		bytes_to_send = BLOB_DATA_MBR_SIZE(cur_blob);
		bytes_sent = sendto(sck->socket, BLOB_DATA_MBR_addr(cur_blob), bytes_to_send, MSG_NOSIGNAL, NULL, 0);
	    }

	    if (bytes_sent == -1) {
		WARN_ERRNO("Sending %zd bytes to %s failed", bytes_to_send, sck->to_string);
		enqueue_blob_for_disk_writing(self, cur_blob);
		close(sck->socket);
		RELAY_ATOMIC_INCREMENT(self->counters.error_count, 1);
		sck = NULL;
		break;		/* stop sending from the hijacked queue */
	    } else if (bytes_sent < bytes_to_send) {
		WARN("We wrote only %zd of %zd bytes to the socket?", bytes_sent, bytes_to_send);
		RELAY_ATOMIC_INCREMENT(self->counters.partial_count, 1);
	    } else {
		RELAY_ATOMIC_INCREMENT(self->counters.sent_count, 1);
	    }
	    blob_destroy(cur_blob);
	}
	cork(sck, 0);

	get_time(&send_end_time);
	if (spilled) {
	    WARN("Wrote %lu items which were over spill threshold", spilled);
	    spilled = 0;
	}

	/* this assumes end_time >= start_time */
	usec = elapsed_usec(&send_start_time, &send_end_time);
	RELAY_ATOMIC_INCREMENT(self->counters.send_elapsed_usec, usec);

	(void) accumulate_and_clear_stats(&self->counters, &self->totals);

	/*
	   SAY("worker[%s] count: %lu sent usec: " STATSfmt,
	   sck->to_string, sent_count, usec/sent_count);
	 */
    }

    if (sck)
	close(sck->socket);

    (void) accumulate_and_clear_stats(&self->counters, &self->totals);

    SAY("worker[%s] processed %lu packets in its lifetime",
	(sck ? sck->to_string : self->arg), RELAY_ATOMIC_READ(self->totals.received_count));

    /* we are done so shut down our "pet" disk worker, and then exit with a message */
    RELAY_ATOMIC_OR(self->disk_writer->exit, EXIT_FLAG);

    /* XXX handle failure of the disk_write shutdown */
    join_err = pthread_join(self->disk_writer->tid, NULL);

    if (join_err)
	WARN("shutting down disk_writer thread error: %d", join_err);

    free(self->disk_writer);

    return NULL;
}


/* initialize a worker safely */
worker_t *worker_init(char *arg)
{
    worker_t *worker = calloc_or_die(sizeof(*worker));
    disk_writer_t *disk_writer = calloc_or_die(sizeof(disk_writer_t));
    int create_err;

    worker->exists = 1;
    worker->arg = strdup(arg);

    socketize(arg, &worker->s_output, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "sender");

    worker->disk_writer = disk_writer;

    disk_writer->pcounters = &worker->counters;
    disk_writer->ptotals = &worker->totals;

    /* setup spillway_path */
    if (snprintf
	(disk_writer->spillway_path, PATH_MAX, "%s/%s", CONFIG.spillway_root, worker->s_output.arg_clean) >= PATH_MAX)
	DIE_RC(EXIT_FAILURE, "spillway_path too big, had to be truncated: %s", disk_writer->spillway_path);


    /* Create the disk_writer before we create the main worker.
     * We do this because the disk_writer only consumes things
     * that have been handled by the main worker, and vice versa
     * when the main worker fails to send then it might want to give
     * the item to the disk worker. If we did it the other way round
     * we might have something to assign to the disk worker but no
     * disk worker to assign it to.
     */
    create_err = pthread_create(&disk_writer->tid, NULL, disk_writer_thread, disk_writer);
    if (create_err)
	DIE_RC(EXIT_FAILURE, "failed to create disk worker errno: %d", create_err);

    /* and finally create the thread */
    create_err = pthread_create(&worker->tid, NULL, worker_thread, worker);
    if (create_err) {
	int join_err;

	/* we died, so shut down our "pet" disk worker, and then exit with a message */
	RELAY_ATOMIC_OR(disk_writer->exit, EXIT_FLAG);

	/* have to handle failure of the shutdown too */
	join_err = pthread_join(disk_writer->tid, NULL);

	if (join_err) {
	    DIE_RC(EXIT_FAILURE,
		   "failed to create socket worker, errno: %d, and also failed to join disk worker, errno: %d",
		   create_err, join_err);
	} else {
	    DIE_RC(EXIT_FAILURE, "failed to create socket worker, errno: %d, disk worker shut down ok", create_err);

	}
    }

    /* return the worker */
    return worker;
}

/* destroy a worker */
void worker_destroy(worker_t * worker)
{
    uint32_t old_exit = RELAY_ATOMIC_OR(worker->exit, EXIT_FLAG);

    /* why is this needed */
    if (old_exit & EXIT_FLAG)
	return;

    pthread_join(worker->tid, NULL);

    free(worker->arg);
    free(worker);
}
