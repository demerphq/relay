#include "worker.h"
#include "worker_pool.h"

/* this is our POOL lock and state object. aint globals lovely. :-) */
extern worker_pool_t POOL;
extern struct config CONFIG;

/* worker sleeps while it waits for work
 * this should be configurable */
void w_wait(int ms) {
    usleep(ms * 1000);
}

/* update the process status line with the send performce of the workers */
void add_worker_stats_to_ps_str(char *str, ssize_t len) {
    worker_t *w;
    int w_num= 0;
    int wrote_len=0 ;
    stats_count_t send_elapsed_usec;
    stats_count_t total;

    LOCK(&POOL.lock);
    TAILQ_FOREACH(w, &POOL.workers, entries) {
        if (!len) break;
        send_elapsed_usec= RELAY_ATOMIC_READ(w->totals.send_elapsed_usec);
        total= RELAY_ATOMIC_READ(w->totals.sent_count);
        if (send_elapsed_usec && total)
            wrote_len= snprintf(str, len, " w%d:" STATSfmt, ++w_num, send_elapsed_usec / total);
        else
            wrote_len= snprintf(str, len, " w%d:-1", ++w_num);

        if (wrote_len < 0 || wrote_len >= len)
            break;
        str += wrote_len;
        len -= wrote_len;
    }
    UNLOCK(&POOL.lock);
}


/* create a directory with the right permissions or throw an exception
 * (not sure the exception makes sense)
 */
static void recreate_fallback_path(char *dir) {
    if (mkdir(dir,0750) == -1 && errno != EEXIST)
        DIE_RC(EXIT_FAILURE,"mkdir of %s failed", dir);
}


/* add an item to a disk worker queue */
static void enqueue_for_disk_writing(worker_t *worker, struct blob *b) {
    b->fallback = strdup(worker->fallback_path); // the function shoyld be called
                                                 // only from/on not-destructed worker
                                                 // and since the destruction path
                                                 // requires that the worker is joined
                                                 // we do not need to put that in the
                                                 // critical section
    q_append(&POOL.disk_writer->queue, b, &POOL.lock);
}

/* if a worker failed to send we need to write the item to the disk
 * (so we can move on) */
static void deal_with_failed_send(worker_t *worker, queue_t *q) {
    blob_t *b;
    for (b = q_shift_nolock(q); b != NULL; b = q_shift_nolock(q)) {
        enqueue_for_disk_writing(worker,b);
    }
}

/* create a normal relay worker thread
 * main loop for the worker process */
void *worker_thread( void *arg ) {
    worker_t *self = (worker_t *) arg;

    queue_t private_queue;
    queue_t spill_queue;

    queue_t *main_queue = &self->queue;
    struct sock *sck= NULL;

    blob_t *cur_blob;
    int join_err;

    memset( &private_queue, 0, sizeof( private_queue ) );
    memset( &spill_queue, 0, sizeof( spill_queue ) );

    while( !RELAY_ATOMIC_READ(self->exit) ) {
        mytime_t send_start_time;
        mytime_t send_end_time;
        mytime_t now;
        uint64_t usec;

        /* check if we have a usable socket */
        if ( !sck ) {
            /* nope, so lets try to open one */
            if ( open_socket( &self->s_output, DO_CONNECT | DO_NOT_EXIT, 0, 0 ) ) {
                /* success, setup sck variable as a flag and save on some indirection */
                sck = &self->s_output;
            } else {
                /* no socket - wait a while, and then redo the loop */
                w_wait( CONFIG.sleep_after_disaster_ms );
                continue;
            }
        }
        assert( sck );

        /* if we dont have anything in our local queue we need to hijack the main one */
        if ( private_queue.head == NULL ) {
            /* hijack the queue - copy the queue state into our private copy
             * and then reset the queue state to empty. So the formerly
             * shared queue is now private. We only do this if necessary.
             */
            if ( !q_hijack( main_queue, &private_queue, &POOL.lock ) ) {
                /* nothing to do, so sleep a while and redo the loop */
                w_wait( CONFIG.polling_interval_ms );
                continue;
            }
        }

        /* ok, so we have something in our queue to process */
        assert( private_queue.head );

        get_time( &send_start_time );

        cork(s,1);
        while ( private_queue.head != NULL ) {
            ssize_t bytes_sent= -2;
            ssize_t bytes_to_send= 0;
            get_time(&now);

            cur_blob= private_queue.head;

            if (
                elapsed_usec( &BLOB_RECEIVED_TIME(cur_blob), &now) >= 1000000
            ) {
                spill_queue.head= cur_blob;
                while ( BLOB_NEXT(cur_blob) &&
                        elapsed_usec( &BLOB_RECEIVED_TIME(BLOB_NEXT(cur_blob)), &now ) >= 1000000
                ) {
                    cur_blob= BLOB_NEXT(cur_blob);
                    spill_queue.count++;
                }
                spill_queue.tail= cur_blob;
                private_queue.head= cur_blob.next;
                private_queue.count -= spill_queue.count;
                cur_blob.next= NULL;
                cur_blob= private_queue.head;

                /* XXX */
                WARN( "Encountered %lu items which were over spill threshold, writing to disk",
                        spill_queue.count );

                enqueue_for_disk_writing( self, spill_queue.head );

                RELAY_ATOMIC_INCREMENT( self->counters.spilled_count, spill_queue.count );

                if (!cur_blob)
                    continue;
            }


            if ( sck->type == SOCK_DGRAM ) {
                bytes_to_send= BLOB_BUF_SIZE( cur_blob );
                bytes_sent= sendto( sck->socket, BLOB_BUF_addr(cur_blob), bytes_to_send,
                        MSG_NOSIGNAL, (struct sockaddr*) &sck->sa.in, sck->addrlen );
            } else {
                bytes_to_send= BLOB_DATA_MBR_SIZE(cur_blob);
                bytes_sent= sendto( sck->socket, BLOB_DATA_MBR_addr(cur_blob), bytes_to_send,
                        MSG_NOSIGNAL, NULL, 0 );
            }

            if ( bytes_sent == -1 ) {
                WARN_ERRNO("Send to %s failed %ld",sck->to_string, BLOB_DATA_MBR_SIZE(cur_blob));
                enqueue_for_disk_writing( self, cur_blob );
                close(sck->socket);
                RELAY_ATOMIC_INCREMENT( self->counters.error_count, 1 );
                sck= NULL;
                break; /* stop sending from the hijacked queue */
            }
            else if ( bytes_sent < bytes_to_send ) {
                WARN( "We wrote only %zd of %zd bytes to the socket?", bytes_sent, bytes_to_send );
                RELAY_ATOMIC_INCREMENT( self->counters.partial_count, 1 );
            } else {
                RELAY_ATOMIC_INCREMENT( self->counters.sent_count, 1 );
            }
            b_destroy( cur_blob );
        }
        cork( sck, 0 );

        get_time( &send_end_time );

        /* this assumes end_time >= start_time */
        usec= elapsed_usec( &send_start_time, &send_end_time );
        RELAY_ATOMIC_INCREMENT( self->counters.send_elapsed_usec, usec );

        (void)snapshot_stats( &self->counters, &self->totals );

        /*
        SAY("worker[%s] count: " STATSfmt " sent usec: " STATSfmt,
                sck->to_string, sent_count, usec/sent_count);
        */
    }

    if (sck)
        close( sck->socket );

    (void)snapshot_stats( &self->counters, &self->totals );

    SAY( "worker[%s] processed " STATSfmt " packets in its lifetime",
            sck->to_string, RELAY_ATOMIC_READ( self->totals.received_count ) );

    /* we are done so shut down our "pet" disk worker, and then exit with a message */
    RELAY_ATOMIC_OR( self->disk_writer->exit, EXIT_FLAG );

    /* XXX handle failure of the disk_write shutdown */
    join_err= pthread_join( self->disk_writer->tid, NULL );

    if (join_err)
        WARN( "shutting down disk_writer thread error: %d", join_err );

    return NULL;
}


/* initialize a worker safely */
worker_t * worker_init(char *arg) {
    worker_t *worker = mallocz_or_die(sizeof(*worker));
    disk_writer_t *disk_writer= mallocz_or_die(sizeof(disk_writer_t));
    int create_err;

    worker->exists = 1;
    worker->arg = strdup(arg);

    /* socketize */
    socketize(arg, &worker->s_output);

    worker->disk_writer= disk_writer;

    /* setup fallback_path */
    if ( snprintf(disk_writer->fallback_path, PATH_MAX,
                "%s/%s/", CONFIG.fallback_root, worker->s_output.to_string) >= PATH_MAX )
        DIE_RC(EXIT_FAILURE,"fallback_path too big, had to be truncated: %s", worker->fallback_path);

    recreate_fallback_path(disk_writer->fallback_path);

    /* Create the disk_writer before we create the main worker.
     * We do this because the disk_writer only consumes things
     * that have been handled by the main worker, and vice versa
     * when the main worker fails to send then it might want to give
     * the item to the disk worker. If we did it the other way round
     * we might have something to assign to the disk worker but no
     * disk worker to assign it to.
     */
    create_err= pthread_create( &disk_writer->tid, NULL, disk_writer_thread, disk_writer );
    if ( create_err )
        DIE_RC( EXIT_FAILURE, "failed to create disk worker errno: %d", create_err );

    /* and finally create the thread */
    create_err= pthread_create( &worker->tid, NULL, worker_thread, worker );
    if ( create_err ) {
        int join_err;

        /* we died, so shut down our "pet" disk worker, and then exit with a message */
        RELAY_ATOMIC_OR( disk_writer->exit, EXIT_FLAG );

        /* have to handle failure of the shutdown too */
        join_err= pthread_join( disk_writer->tid, NULL );

        if (join_err) {
            DIE_RC( EXIT_FAILURE,
                    "failed to create socket worker, errno: %d, and also failed to join disk worker, errno: %d",
                    create_err, join_err );
        } else {
            DIE_RC( EXIT_FAILURE,
                    "failed to create socket worker, errno: %d, disk worker shut down ok",
                    create_err );

        }
    }

    /* return the worker */
    return worker;
}

/* destroy a worker */
void worker_destroy(worker_t *worker) {
    uint32_t old_exit= RELAY_ATOMIC_OR(worker->exit, EXIT_FLAG);

    if (old_exit & EXIT_FLAG)
        return;

    pthread_join(worker->tid, NULL);

    /* huh? why do we do this in the parent thread? */
    if (worker->s_output.socket) {
        close(worker->s_output.socket);
        deal_with_failed_send(worker, &worker->queue); /* XXX: cant be right! */
    }
    free(worker->arg);
    free(worker);
}

