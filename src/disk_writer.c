#include "disk_writer.h"
#include "worker_pool.h"
#include "config.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;
extern struct config CONFIG;

/* create a directory with the right permissions or throw an exception
 * (not sure the exception makes sense)
 */
static void recreate_fallback_path(char *dir) {
    if (mkdir(dir,0750) == -1 && errno != EEXIST)
        DIE_RC(EXIT_FAILURE,"mkdir of %s failed", dir);
}

static void setup_for_epoch(disk_writer_t *self, time_t blob_epoch) {
    if (self->last_epoch == blob_epoch)
        return;
    if (self->last_epoch) {
        if (fsync(self->fd)) {
            WARN_ERRNO("failed to fsync '%s', everyting is lost!", self->last_file_path);
        }
        if (close(self->fd)) {
            WARN_ERRNO("failed to close '%s', everyting is lost!", self->last_file_path);
        }
    }
    if (blob_epoch) {
        if (snprintf(self->last_file_path, PATH_MAX, "%s/%li.srlc",
                     self->fallback_path, blob_epoch) >= PATH_MAX
        ) {
            /* XXX: should this really die? */
            DIE_RC(EXIT_FAILURE,"filename was truncated to %d bytes: '%s'",
                    PATH_MAX, self->last_file_path);
        }
        recreate_fallback_path(self->fallback_path);
        self->fd = open(self->last_file_path, O_WRONLY|O_APPEND|O_CREAT, 0640);
        if (self->fd < 0) {
            WARN_ERRNO("failed to open '%s', everyting is lost!", self->last_file_path);
            blob_epoch= 0;
        }
    }
    self->last_epoch= blob_epoch;
}

/* write a blob to disk */
static void write_blob_to_disk(disk_writer_t *self, blob_t *b) {
    assert(BLOB_REF_PTR(b));
    
    setup_for_epoch(self, BLOB_RECEIVED_TIME(b).tv_sec);

    if ( self->fd >= 0 ) {
        ssize_t wrote= write(self->fd, BLOB_BUF(b), BLOB_BUF_SIZE(b));
        if (wrote != BLOB_BUF_SIZE(b) ) {
            WARN_ERRNO("Wrote only %ld of %i bytes to '%s', error:",
                    wrote, BLOB_BUF_SIZE(b), self->last_file_path);
        }
    }
}


/* create a disk writer worker thread
 * main loop for the disk writer worker process */
void *disk_writer_thread(void *arg) {
    disk_writer_t *self = (disk_writer_t *)arg;

    queue_t private_queue;
    queue_t *main_queue = &self->queue;
    blob_t *b;
    int done_work= 0;
    
    recreate_fallback_path(self->fallback_path);
    SAY("disk writer started using path '%s' for files", self->fallback_path);

    memset(&private_queue, 0, sizeof(private_queue));

    while( 1 ){

        q_hijack(main_queue, &private_queue, &POOL.lock);
        b= private_queue.head;

        if ( b == NULL ) {
            if (done_work) {
                SAY("cleared disk queue");
                done_work= 0;
            }

            setup_for_epoch(self, 0);
            if (RELAY_ATOMIC_READ(self->exit)) {
                /* nothing to do and we have been asked to exit, so break from the loop */
                break;
            }
            else {
                w_wait( CONFIG.polling_interval_ms );
            }
        } else {
            done_work= 1;
            do {
                write_blob_to_disk(self, b);
                b_destroy( q_shift_nolock( &private_queue) );
                RELAY_ATOMIC_INCREMENT( self->counters.disk_count, 1 );
            } while ((b = private_queue.head) != NULL);

            (void)snapshot_stats( &self->counters, &self->totals );
        }
    }

    (void)snapshot_stats( &self->counters, &self->totals );

    SAY("disk_writer saved " STATSfmt " packets in its lifetime", self->totals.disk_count);

    return NULL;
}
