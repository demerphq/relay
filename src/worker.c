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
        _ENO("setsockopt: %s", strerror(errno));
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

/* append an item to queue safely */
int q_append_locked(worker_t *worker, blob_t *b) {
    struct queue *q = &worker->queue;

    if (q->head == NULL)
        q->head = b;
    else
        BLOB_NEXT_set(q->tail,b);

    q->tail = b;
    BLOB_NEXT_set(b,NULL);
    q->count++;
    return 1;
}

/* shift an item out of a queue non-safely */
blob_t *q_shift_nolock(struct queue *q) {
    blob_t *b= q->head;
    if (b) {
        if (BLOB_NEXT(b))
            q->head = BLOB_NEXT(b);
        else
            q->head = q->tail = NULL;
        q->count--;
    }
    return b;
}

/* create a directory with the right permissions or throw an exception
 * (not sure the exception makes sense)
 */
static void recreate_fallback_path(char *dir) {
    if (mkdir(dir,0750) == -1 && errno != EEXIST)
        SAYX(EXIT_FAILURE,"mkdir of %s failed", dir);
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
        SAYX(EXIT_FAILURE,"filename was truncated to %d bytes", PATH_MAX);
    }
    fd = open(file, O_WRONLY|O_APPEND|O_CREAT, 0640);
    if (fd < 0)
        _ENO("failed to open '%s', everyting is lost!", file);

    if (write(fd, BLOB_BUF(b), BLOB_BUF_SIZE(b)) != BLOB_BUF_SIZE(b))
        _ENO("failed to write '%s', everyting is lost!", file);

    if (fsync(fd))
        _ENO("failed to fsync '%s', everyting is lost!", file);
    if (close(fd))
        _ENO("failed to close '%s', everyting is lost!", file);
}

/* add an item to a disk worker queue */
static void enqueue_for_disk_writing(worker_t *worker, struct blob *b) {
    b->fallback = strdup(worker->fallback_path); // the function shoild be called
                                                 // only from/on not-destructed worker
                                                 // and since the destruction path
                                                 // requires that the worker is joined
                                                 // we do not need to put that in the
                                                 // critical section
    LOCK(&GIANT.lock);
    q_append_locked(GIANT.disk_writer,b);
    UNLOCK(&GIANT.lock);
}

/* if a worker failed to send we need to write the item to the disk
 * (so we can move on) */
static void deal_with_failed_send(worker_t *worker, struct queue *q) {
    blob_t *b;
    for (b = q_shift_nolock(q); b != NULL; b = q_shift_nolock(q)) {
        enqueue_for_disk_writing(worker,b);
    }
}

/* create a normal relay worker thread
 * main loop for the worker process */
void *worker_thread(void *arg) {
    worker_t *self = (worker_t *) arg;
    struct queue hijacked_queue;
    struct queue *q = &self->queue;
    struct sock *s = &self->s_output;
    stats_count_t total_tmp;
    blob_t *b;

    memset(&hijacked_queue, 0, sizeof(hijacked_queue));
again:
    while(
        !RELAY_ATOMIC_READ(self->exit) &&
        !open_socket(s, DO_CONNECT | DO_NOT_EXIT,0,0)
    ) {
        w_wait(CONFIG.sleep_after_disaster_ms);
    }

    while(!RELAY_ATOMIC_READ(self->exit)) {
        /* hijack the queue - copy the queue state into our private copy
         * and then reset the queue state to empty. So the formerly
         * shared queue is now private. We only do this if necessary. */
        if (hijacked_queue.head == NULL) {
            LOCK(&GIANT.lock);
            memcpy(&hijacked_queue, q, sizeof(struct queue));
            q->tail = q->head = NULL;
            q->count = 0;
            UNLOCK(&GIANT.lock);
        }
        if (hijacked_queue.head == NULL) {
            w_wait(CONFIG.polling_interval_ms);
        } else {
            struct timeval start_time;
            struct timeval end_time;
            uint32_t elapsed_usec= 0;

            gettimeofday(&start_time, NULL);
            cork(s,1);
            while ((b = hijacked_queue.head) != NULL) {
                if (SEND(s,b) < 0) {
                    _ENO("ABORT: send to %s failed %ld",s->to_string, BLOB_DATA_MBR_SIZE(b));
                    deal_with_failed_send(self, &hijacked_queue);
                    close(s->socket);
                    goto again;
                }
                b_destroy( q_shift_nolock( &hijacked_queue ) );
                RELAY_ATOMIC_INCREMENT(self->counters.count,1);
            }
            cork(s,0);
            (void)snapshot_stats(&self->counters, &total_tmp);

            gettimeofday(&end_time, NULL);

            /* this assumes end_time >= start_time */
            elapsed_usec= ( ( end_time.tv_sec - start_time.tv_sec) * 1000000 )
                          + end_time.tv_usec - start_time.tv_usec;

            RELAY_ATOMIC_INCREMENT(self->counters.elapsed_usec, elapsed_usec);

        }
    }
    close(s->socket);
    (void)snapshot_stats(&self->counters,&total_tmp);
    _D("worker[%s] sent " STATSfmt " packets in its lifetime", s->to_string, total_tmp);
    return NULL;
}


/* create a disk writer worker thread
 * main loop for the disk writer worker process */
static void *disk_writer_thread(void *arg) {
    worker_t *self = (worker_t *) arg;
    struct queue hijacked_queue;
    struct queue *q = &self->queue;
    stats_count_t total_tmp;
    blob_t *b;
    _D("disk writer started");
    memset(&hijacked_queue, 0, sizeof(hijacked_queue));
    while(!RELAY_ATOMIC_READ(self->exit)) {
        LOCK(&GIANT.lock);
        memcpy(&hijacked_queue, q, sizeof(struct queue));
        q->tail = q->head = NULL;
        q->count = 0;
        UNLOCK(&GIANT.lock);
        while ((b = hijacked_queue.head) != NULL) {
            write_blob_to_disk(b);
            b_destroy( q_shift_nolock( &hijacked_queue ) );
            RELAY_ATOMIC_INCREMENT(self->counters.count,1);
        }
        (void)snapshot_stats(&self->counters,&total_tmp);
        w_wait(CONFIG.polling_interval_ms);
    }
    (void)snapshot_stats(&self->counters,&total_tmp);
    _D("disk_writer saved " STATSfmt " packets in its lifetime", total_tmp);
    return NULL;
}


/* add an item to all workers queues
 * (not sure if this really belongs in worker.c)
 */
int enqueue_blob_for_transmission(blob_t *b) {
    int i = 0;
    worker_t *w;
    LOCK(&GIANT.lock);
    BLOB_REFCNT_set(b,GIANT.n_workers);
    TAILQ_FOREACH(w, &GIANT.workers, entries) {
        /* check if this item is no the last */
        if ( TAILQ_NEXT(w, entries) == NULL ) {
            /* this is the last item in the chain, just use b. */
            q_append_locked(w, b);
        } else {
            /* not the last, so we need to clone the original object */
            q_append_locked(w, b_clone_no_refcnt_inc(b));
        }
        i++;
    }
    UNLOCK(&GIANT.lock);
    if (i == 0) {
        _E("no living workers, not sure what to do"); // dump the packet on disk?
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
        SAYX(EXIT_FAILURE,"fallback_path too big, had to be truncated: %s", worker->fallback_path);
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
