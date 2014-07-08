#include "worker.h"

/* this is our GIANT lock and state object. aint globals lovely. :-)*/
static struct giant {
    /* macro to define a TAILQ head entry, empty first arg deliberate */
    TAILQ_HEAD(, worker) workers;
    LOCK_T lock;
    worker_t *disk_writer;
    int n_workers;
} GIANT;
extern struct config CONFIG;

#ifdef TCP_CORK
/* try to get the OS to send our packets more efficiently when sending
 * via TCP. */
static INLINE void cork(struct sock *s,int flag) {
    if (!s || s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        WARN_ERRNO("setsockopt: %s", strerror(errno));
}
#else
#define cork(a,b)
#endif

/* update the process status line with the send performce of the workers */
void add_worker_stats_to_ps_str(char *str, ssize_t len) {
    worker_t *w;
    int w_num= 0;
    int wrote_len=0 ;
    stats_count_t elapsed_usec;
    stats_count_t total;

    LOCK(&GIANT.lock);
    TAILQ_FOREACH(w, &GIANT.workers, entries) {
        if (!len) break;
        elapsed_usec= RELAY_ATOMIC_READ(w->counters.elapsed_usec);
        total= RELAY_ATOMIC_READ(w->counters.total);
        if (elapsed_usec && total)
            wrote_len= snprintf(str, len, " w%d:" STATSfmt, ++w_num, elapsed_usec / total);
        else
            wrote_len= snprintf(str, len, " w%d:-1", ++w_num);

        if (wrote_len < 0 || wrote_len >= len)
            break;
        str += wrote_len;
        len -= wrote_len;
    }
    UNLOCK(&GIANT.lock);
}


/* create a directory with the right permissions or throw an exception
 * (not sure the exception makes sense)
 */
static void recreate_fallback_path(char *dir) {
    if (mkdir(dir,0750) == -1 && errno != EEXIST)
        DIE_RC(EXIT_FAILURE,"mkdir of %s failed", dir);
}

/* write a blob to disk */
static void write_blob_to_disk(blob_t *b) {
    assert(BLOB_REF_PTR(b));
    assert(b->fallback);
    char file[PATH_MAX];
    int fd;
    recreate_fallback_path(b->fallback);
    if (snprintf(file, PATH_MAX, "%s/%li.srlc",
                 b->fallback,
                 (long int)time(NULL)) >= PATH_MAX) {
        DIE_RC(EXIT_FAILURE,"filename was truncated to %d bytes", PATH_MAX);
    }
    fd = open(file, O_WRONLY|O_APPEND|O_CREAT, 0640);
    if (fd < 0)
        WARN_ERRNO("failed to open '%s', everyting is lost!", file);

    if (write(fd, BLOB_BUF(b), BLOB_BUF_SIZE(b)) != BLOB_BUF_SIZE(b))
        WARN_ERRNO("failed to write '%s', everyting is lost!", file);

    if (fsync(fd))
        WARN_ERRNO("failed to fsync '%s', everyting is lost!", file);
    if (close(fd))
        WARN_ERRNO("failed to close '%s', everyting is lost!", file);
}

/* add an item to a disk worker queue */
static void enqueue_for_disk_writing(worker_t *worker, struct blob *b) {
    b->fallback = strdup(worker->fallback_path); // the function shoild be called
                                                 // only from/on not-destructed worker
                                                 // and since the destruction path
                                                 // requires that the worker is joined
                                                 // we do not need to put that in the
                                                 // critical section
    q_append(&GIANT.disk_writer->queue, b, &GIANT.lock);
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
void *worker_thread(void *arg) {
    worker_t *self = (worker_t *) arg;
    queue_t private_queue;
    queue_t *main_queue = &self->queue;
    struct sock *sck= NULL;

    blob_t *cur_blob;

    memset(&private_queue, 0, sizeof(private_queue));

    while(!RELAY_ATOMIC_READ(self->exit)) {
        mytime_t send_start_time;
        mytime_t send_end_time;
        mytime_t now;
        uint64_t usec;

        /* check if we have a usable socket */
        if (!sck) {
            /* nope, so lets try to open one */
            if (open_socket(&self->s_output, DO_CONNECT | DO_NOT_EXIT, 0, 0)) {
                /* success, setup sck variable as a flag and save on some indirection */
                sck = &self->s_output;
            } else {
                /* no socket - wait a while, and then redo the loop */
                w_wait(CONFIG.sleep_after_disaster_ms);
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
            if ( !q_hijack(main_queue, &private_queue, &GIANT.lock) ) {
                /* nothing to do, so sleep a while and redo the loop */
                w_wait(CONFIG.polling_interval_ms);
                continue;
            }
        }

        /* ok, so we have something in our queue to process */
        assert(private_queue.head);

        get_time(&send_start_time);

        cork(s,1);
        while ( ( cur_blob = q_shift_nolock( &private_queue ) ) != NULL) {
            ssize_t bytes_sent= -2;
            ssize_t bytes_to_send= 0;

            get_time(&now);
            usec= elapsed_usec(&BLOB_RECEIVED_TIME(b),&now);
            if (usec <= 1000000) {
                if (sck->type == SOCK_DGRAM) {
                    bytes_to_send= BLOB_BUF_SIZE(cur_blob);
                    bytes_sent= sendto(sck->socket, BLOB_BUF_addr(cur_blob), bytes_to_send,
                            MSG_NOSIGNAL, (struct sockaddr*) &sck->sa.in, sck->addrlen)
                } else {
                    bytes_to_send= BLOB_DATA_MBR_SIZE(cur_blob);
                    bytes_sent= sendto(sck->socket, BLOB_DATA_MBR_addr(cur_blob), bytes_to_send,
                            MSG_NOSIGNAL, NULL, 0);
                }
            }

            if (bytes_sent == -1) {
                WARN_ERRNO("Send to %s failed %ld",sck->to_string, BLOB_DATA_MBR_SIZE(cur_blob));
                enqueue_for_disk_writing(worker, cur_blob);
                close(sck->socket);
                RELAY_ATOMIC_INCREMENT(self->counters.error_count, 1);
                sck= NULL;
                break; /* stop sending from the hijacked queue */
            }
            else
            if (bytes_sent == -2) {
                WARN("Item is %d which is over spill threshold, writing to disk", usec);
                enqueue_for_disk_writing(worker, cur_blob);
                RELAY_ATOMIC_INCREMENT(self->counters.spill_count, 1);
            }
            else {
                if (bytes_sent < bytes_to_send) {
                    WARN("We wrote only %d of %d bytes to the socket?", bytes_sent, bytes_to_send);
                    RELAY_ATOMIC_INCREMENT(self->counters.partial_count, 1);
                } else {
                    RELAY_ATOMIC_INCREMENT(self->counters.sent_count, 1);
                }
                b_destroy(cur_blob);
            }
        }
        cork(sck,0);

        get_time(&send_end_time);

        /* this assumes end_time >= start_time */
        usec= elapsed_usec(&send_start_time, &send_end_time);
        RELAY_ATOMIC_INCREMENT(self->counters.elapsed_usec, usec);

        (void)snapshot_stats(&self->counters, &self->totals);

        /*
        SAY("worker[%s] count: " STATSfmt " sent usec: " STATSfmt,
                sck->to_string, sent_count, usec/sent_count);
        */
    }
    if (sck)
        close(sck->socket);

    (void)snapshot_stats( &self->counters, &self->totals );

    SAY("worker[%s] processed " STATSfmt " packets in its lifetime",
            sck->to_string, RELAY_ATOMIC_READ(self->totals.received_count));
    return NULL;
}


/* create a disk writer worker thread
 * main loop for the disk writer worker process */
static void *disk_writer_thread(void *arg) {
    worker_t *self = (worker_t *) arg;
    queue_t private_queue;
    queue_t *main_queue = &self->queue;
    stats_count_t total_tmp;
    blob_t *b;
    SAY("disk writer started");
    memset(&private_queue, 0, sizeof(private_queue));
    while(!RELAY_ATOMIC_READ(self->exit)) {

        q_hijack(main_queue, &private_queue, &GIANT.lock);

        while ((b = private_queue.head) != NULL) {
            write_blob_to_disk(b);
            b_destroy( q_shift_nolock( &private_queue) );
            RELAY_ATOMIC_INCREMENT(self->counters.count,1);
        }
        (void)snapshot_stats(&self->counters,&total_tmp);
        w_wait(CONFIG.polling_interval_ms);
    }
    (void)snapshot_stats(&self->counters,&total_tmp);
    SAY("disk_writer saved " STATSfmt " packets in its lifetime", total_tmp);
    return NULL;
}


/* add an item to all workers queues
 * (not sure if this really belongs in worker.c)
 */
int enqueue_blob_for_transmission(blob_t *b) {
    int i = 0;
    worker_t *w;
    blob_t *to_enqueue;
    LOCK(&GIANT.lock);
    BLOB_REFCNT_set(b,GIANT.n_workers);
    TAILQ_FOREACH(w, &GIANT.workers, entries) {
        /* check if this item is no the last */
        if ( TAILQ_NEXT(w, entries) == NULL ) {
            /* this is the last item in the chain, just use b. */
            to_enqueue= b;
        } else {
            /* not the last, so we need to clone the original object */
            to_enqueue= b_clone_no_refcnt_inc(b);
        }
        q_append_nolock(&w->queue, to_enqueue);

        i++;
    }
    UNLOCK(&GIANT.lock);
    if (i == 0) {
        WARN("no living workers, not sure what to do"); // dump the packet on disk?
    }
    return i;
}

/* worker sleeps while it waits for work
 * this should be configurable */
void w_wait(int ms) {
    usleep(ms * 1000);
}

/* create a new worker object */
worker_t *worker_new(void) {
    worker_t *worker = malloc_or_die(sizeof(*worker));

    /* wipe worker */
    memset(worker,0,sizeof(*worker));

    /* setup flags */
    worker->exists = 1;
    return worker;
}

/* initialize a worker safely */
worker_t * worker_init_locked(char *arg) {
    worker_t *worker = worker_new();

    worker->arg = strdup(arg);

    /* socketize */
    socketize(arg, &worker->s_output);

    /* setup fallback_path */
    if (snprintf(worker->fallback_path, PATH_MAX,"%s/%s/", CONFIG.fallback_root,worker->s_output.to_string) >= PATH_MAX)
        DIE_RC(EXIT_FAILURE,"fallback_path too big, had to be truncated: %s", worker->fallback_path);
    recreate_fallback_path(worker->fallback_path);
    /* and finally create the thread */
    pthread_create(&worker->tid, NULL, worker_thread, worker);

    /* return the worker */
    return worker;
}

/* destroy a worker */
void worker_destroy(worker_t *worker) {
    uint32_t old_exit= RELAY_ATOMIC_OR(worker->exit,EXIT_FLAG);

    if (old_exit & EXIT_FLAG)
        return;

    pthread_join(worker->tid, NULL);
    if (worker->s_output.socket) {
        close(worker->s_output.socket);
        deal_with_failed_send(worker, &worker->queue);
    }
    free(worker->arg);
    free(worker);
}

/* initialize a pool of worker
 * imo this has a crap name
 */
void worker_init_static(int argc, char **argv, int reload) {
    int i;
    int must_add;
    worker_t *w,*wtmp;
    int n_workers = 0;
    if (reload) {
        LOCK(&GIANT.lock);

        TAILQ_FOREACH(w, &GIANT.workers, entries) {
            w->exists = 0;
        }
        for (i = 0; i < argc; i++) {
            must_add = 1;
            TAILQ_FOREACH(w, &GIANT.workers, entries) {
                if (strcmp(argv[i], w->arg) == 0) {
                    w->exists = 1;
                    must_add = 0;
                }
            }
            if (must_add) {
                w = worker_init_locked(argv[i]);
                TAILQ_INSERT_TAIL(&GIANT.workers, w, entries);
            }
        }
        TAILQ_FOREACH_SAFE(w,&GIANT.workers,entries,wtmp) {
            if (w->exists == 0) {
                TAILQ_REMOVE(&GIANT.workers, w, entries);
                UNLOCK(&GIANT.lock);

                worker_destroy(w); // might lock

                LOCK(&GIANT.lock);
            } else {
                n_workers++;
            }
        }
        GIANT.n_workers = n_workers;
        UNLOCK(&GIANT.lock);
    } else {
        TAILQ_INIT(&GIANT.workers);
        LOCK_INIT(&GIANT.lock);

        // spawn the disk writer thread
        GIANT.disk_writer = worker_new();
        pthread_create(&GIANT.disk_writer->tid, NULL, disk_writer_thread, GIANT.disk_writer);

        LOCK(&GIANT.lock);
        GIANT.n_workers = 0;
        for (i = 0; i < argc; i++) {
            if (is_aborted())
                break;
            w = worker_init_locked(argv[i]);
            TAILQ_INSERT_HEAD(&GIANT.workers, w, entries);
            GIANT.n_workers++;
        }
        UNLOCK(&GIANT.lock);
    }
}

/* worker destory static, destroy all the workers in the pool */
void worker_destroy_static(void) {
    worker_t *w;
    LOCK(&GIANT.lock);
    while ((w = TAILQ_FIRST(&GIANT.workers)) != NULL) {
        TAILQ_REMOVE(&GIANT.workers, w, entries);
        UNLOCK(&GIANT.lock);

        worker_destroy(w); // might lock

        LOCK(&GIANT.lock);
    }
    UNLOCK(&GIANT.lock);
}

/* shut down the disk writer thread */
void disk_writer_stop(void) {
    worker_destroy(GIANT.disk_writer);
}
