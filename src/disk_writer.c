#include "disk_writer.h"

#include "config.h"
#include "global.h"
#include "log.h"
#include "socket_worker_pool.h"

/* create a directory with the right permissions
 */
static int recreate_spillway_path(char *dir)
{
    if (mkdir(dir, 0750) == -1 && errno != EEXIST) {
	FATAL("mkdir of %s failed", dir);
	return 0;
    }
    return 1;
}

static int setup_for_epoch(disk_writer_t * self, time_t blob_epoch)
{
    if (self->last_epoch == blob_epoch)
	return 1;

    if (self->last_epoch) {
	if (fsync(self->fd)) {
	    FATAL_ERRNO("fsync '%s' failed", self->last_file_path);
	    return 0;
	}
	if (close(self->fd)) {
	    FATAL_ERRNO("close '%s' failed", self->last_file_path);
	    return 0;
	}
    }
    if (blob_epoch) {
	int wrote = snprintf(self->last_file_path, PATH_MAX, "%s/%li.srlc", self->spillway_path, blob_epoch);
	if (wrote < 0 || wrote >= PATH_MAX) {
	    FATAL("Filename was truncated to %d bytes: '%s'", PATH_MAX, self->last_file_path);
	    return 0;
	}
	if (!recreate_spillway_path(self->spillway_path))
	    return 0;
	self->fd = open(self->last_file_path, O_WRONLY | O_APPEND | O_CREAT, 0640);
	if (self->fd < 0) {
	    FATAL_ERRNO("open '%s' failed", self->last_file_path);
	    return 0;
	}
    }
    self->last_epoch = blob_epoch;

    return 1;
}

/* write a blob to disk */
static int write_blob_to_disk(disk_writer_t * self, blob_t * b)
{
    if (BLOB_REF_PTR(b) == NULL) {
	FATAL("write_blob_to_disk: NULL blob");
	return 0;
    }

    if (!setup_for_epoch(self, BLOB_RECEIVED_TIME(b).tv_sec))
	return 0;

    /* TODO: there should be some sort of monitoring/alerting for low disk space:
     * I left this running for half an hour (with a load testing client) and it filled
     * my /tmp disk (/tmp/tcp_localhost_9003/....)  Whether the monitoring belongs
     * in the relay or somewhere else, is a good question. */

    if (self->fd >= 0) {
	ssize_t wrote = write(self->fd, BLOB_BUF(b), BLOB_BUF_SIZE(b));
	if (wrote == BLOB_BUF_SIZE(b)) {
	    RELAY_ATOMIC_INCREMENT(self->counters->disk_count, 1);
	    return 1;
	}
	FATAL_ERRNO("write '%s' failed: wrote %zd tried %d bytes:", self->last_file_path, wrote, BLOB_BUF_SIZE(b));

    }
    RELAY_ATOMIC_INCREMENT(self->counters->disk_error_count, 1);

    return 0;
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

	queue_hijack(main_queue, &private_queue, &GLOBAL.pool.lock);
	b = private_queue.head;

	if (b == NULL) {
	    if (done_work) {
		SAY("cleared disk queue of %d items", done_work);
		done_work = 0;
	    }

	    setup_for_epoch(self, 0);
	    if (RELAY_ATOMIC_READ(self->base.stopping)) {
		/* nothing to do and we have been asked to exit, so break from the loop */
		break;
	    } else {
		worker_wait_millisec(self->base.config->polling_interval_millisec);
	    }
	} else {
	    do {
		done_work++;
		if (!write_blob_to_disk(self, b)) {
		    FATAL("Failed to write blob to disk");
		}
		blob_destroy(queue_shift_nolock(&private_queue));
	    }
	    while ((b = private_queue.head) != NULL);

	    accumulate_and_clear_stats(self->counters, self->recents, self->totals);
	}
    }

    if (control_is(RELAY_STOPPING)) {
	SAY("Stopping, trying disk flush");
	queue_hijack(main_queue, &private_queue, &GLOBAL.pool.lock);
	b = private_queue.head;
	size_t wrote = 0;
	if (b) {
	    SAY("Disk flush starting");
	    do {
		write_blob_to_disk(self, b);
		wrote += b->ref->data.size;
		blob_destroy(queue_shift_nolock(&private_queue));
	    }
	    while ((b = private_queue.head) != NULL);
	} else {
	    SAY("Nothing to disk flush");
	}
	SAY("Disk flush wrote %zd bytes", wrote);
    }

    accumulate_and_clear_stats(self->counters, self->recents, self->totals);

    SAY("disk_writer saved %lu packets in its lifetime", (unsigned long) self->totals->disk_count);

    return NULL;
}
