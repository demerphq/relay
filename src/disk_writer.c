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

/* write a blob to disk */
static void write_blob_to_disk(disk_writer_t *self, blob_t *b) {
    int fd;
    
    assert(BLOB_REF_PTR(b));
    recreate_fallback_path(self->fallback_path);
    
    if (snprintf(self->last_file_path, PATH_MAX, "%s/%li.srlc",
                 self->fallback_path,
                 (long int)BLOB_RECEIVED_TIME(b).tv_sec) >= PATH_MAX) {
        DIE_RC(EXIT_FAILURE,"filename was truncated to %d bytes", PATH_MAX);
    }
    fd = open(self->last_file_path, O_WRONLY|O_APPEND|O_CREAT, 0640);
    if (fd < 0)
        WARN_ERRNO("failed to open '%s', everyting is lost!", self->last_file_path);

    if (write(fd, BLOB_BUF(b), BLOB_BUF_SIZE(b)) != BLOB_BUF_SIZE(b))
        WARN_ERRNO("failed to write '%s', everyting is lost!", self->last_file_path);

    if (fsync(fd))
        WARN_ERRNO("failed to fsync '%s', everyting is lost!", self->last_file_path);
    if (close(fd))
        WARN_ERRNO("failed to close '%s', everyting is lost!", self->last_file_path);
}


/* create a disk writer worker thread
 * main loop for the disk writer worker process */
void *disk_writer_thread(void *arg) {
    disk_writer_t *self = (disk_writer_t *)arg;

    queue_t private_queue;
    queue_t *main_queue = &self->queue;
    blob_t *b;
    
    recreate_fallback_path(self->fallback_path);
    SAY("disk writer started using path '%s' for files", self->fallback_path);

    memset(&private_queue, 0, sizeof(private_queue));

    while( 1 ){

        q_hijack(main_queue, &private_queue, &POOL.lock);
        b= private_queue.head;

        if ( b == NULL ) {
            if (RELAY_ATOMIC_READ(self->exit)) {
                /* nothing to do and we have been asked to exit, so break from the loop */
                break;
            }
            else {
                w_wait( CONFIG.polling_interval_ms );
            }
        } else {
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
