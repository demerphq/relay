#include "disk_writer.h"

#include "config.h"
#include "worker_pool.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;

/* create a directory with the right permissions or throw an exception
 * (not sure the exception makes sense)
 */
static void recreate_spillway_path(char *dir)
{
    if (mkdir(dir, 0750) == -1 && errno != EEXIST)
	DIE_RC(EXIT_FAILURE, "mkdir of %s failed", dir);
}

static void setup_for_epoch(disk_writer_t * self, time_t blob_epoch)
{
    if (self->last_epoch == blob_epoch)
	return;
    if (self->last_epoch) {
	if (fsync(self->fd)) {
	    WARN_ERRNO("failed to fsync '%s', everything is lost!", self->last_file_path);
	}
	if (close(self->fd)) {
	    WARN_ERRNO("failed to close '%s', everything is lost!", self->last_file_path);
	}
    }
    if (blob_epoch) {
	int wrote = snprintf(self->last_file_path, PATH_MAX, "%s/%li.srlc", self->spillway_path, blob_epoch);
	if (wrote < 0 || wrote >= PATH_MAX) {
	    /* XXX: should this really die?
	     * Retry in /tmp? */
	    DIE_RC(EXIT_FAILURE, "filename was truncated to %d bytes: '%s'", PATH_MAX, self->last_file_path);
	}
	recreate_spillway_path(self->spillway_path);
	self->fd = open(self->last_file_path, O_WRONLY | O_APPEND | O_CREAT, 0640);
	if (self->fd < 0) {
	    WARN_ERRNO("failed to open '%s', everything is lost!", self->last_file_path);
	    blob_epoch = 0;
	}
    }
    self->last_epoch = blob_epoch;
}

/* write a blob to disk */
static void write_blob_to_disk(disk_writer_t * self, blob_t * b)
{
    assert(BLOB_REF_PTR(b));

    setup_for_epoch(self, BLOB_RECEIVED_TIME(b).tv_sec);

    /* TODO: there should be some sort of monitoring/alerting for low disk space:
     * I left this running for half an hour (with a load testing client) and it filled
     * my /tmp disk (/tmp/tcp_localhost_9003/....)  Whether the monitoring belongs
     * in the relay or somewhere else, is a good question. */

    if (self->fd >= 0) {
	ssize_t wrote = write(self->fd, BLOB_BUF(b), BLOB_BUF_SIZE(b));
	if (wrote == BLOB_BUF_SIZE(b)) {
	    RELAY_ATOMIC_INCREMENT(self->counters->disk_count, 1);
	    return;
	}
	WARN_ERRNO("Wrote only %ld of %i bytes to '%s', error:", wrote, BLOB_BUF_SIZE(b), self->last_file_path);

    }
    RELAY_ATOMIC_INCREMENT(self->counters->disk_error_count, 1);
}


/* create a disk writer worker thread
 * main loop for the disk writer worker process */
void *disk_writer_thread(void *arg)
{
    disk_writer_t *self = (disk_writer_t *) arg;

    queue_t private_queue;
    queue_t *main_queue = &self->queue;
    blob_t *b;
    uint32_t done_work = 0;

    recreate_spillway_path(self->spillway_path);
    SAY("disk writer started using path '%s' for files", self->spillway_path);

    memset(&private_queue, 0, sizeof(private_queue));

    while (1) {

	queue_hijack(main_queue, &private_queue, &POOL.lock);
	b = private_queue.head;

	if (b == NULL) {
	    if (done_work) {
		SAY("cleared disk queue of %d items", done_work);
		done_work = 0;
	    }

	    setup_for_epoch(self, 0);
	    if (RELAY_ATOMIC_READ(self->base.exiting)) {
		/* nothing to do and we have been asked to exit, so break from the loop */
		break;
	    } else {
		worker_wait_millisec(self->base.config->polling_interval_millisec);
	    }
	} else {
	    do {
		done_work++;
		write_blob_to_disk(self, b);
		blob_destroy(queue_shift_nolock(&private_queue));
	    }
	    while ((b = private_queue.head) != NULL);

	    accumulate_and_clear_stats(self->counters, self->totals);
	}
    }

    accumulate_and_clear_stats(self->counters, self->totals);

    SAY("disk_writer saved %lu packets in its lifetime", (unsigned long) self->totals->disk_count);

    return NULL;
}
