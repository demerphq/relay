#include "relay.h"
#include "worker.h"
#include "setproctitle.h"
#ifdef __linux__
#define TAILQ_EMPTY(head)       ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head)       ((head)->tqh_first)
#ifndef TAILQ_END
#define	TAILQ_END(head)			NULL
#endif
#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
	for ((var) = TAILQ_FIRST(head);                                 \
	    (var) != TAILQ_END(head) &&                                 \
	    ((tvar) = TAILQ_NEXT(var, field), 1);                       \
	    (var) = (tvar))
#endif
#endif

#define EXIT_FLAG 1

static struct giant {
    TAILQ_HEAD(, worker) workers;
    LOCK_T lock;
    pthread_mutex_t cond_lock;
    pthread_cond_t cond;
    int n_workers;
} GIANT;

#ifdef TCP_CORK
static INLINE void cork(struct sock *s,int flag) {
    if (s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        _ENO("setsockopt: %s", strerror(errno));
}
#else
#define cork(a,b)
#endif

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

static void recreate_fallback_path(char *dir) {
    if (mkdir(dir,0750) == -1 && errno != EEXIST)
        SAYX(EXIT_FAILURE,"mkdir of %s failed", dir);
}

int get_epoch_filehandle_locked(worker_t *worker) {
    int fd;
    if (snprintf(worker->fallback_file, PATH_MAX, "%s/%li.srlc",
                 worker->fallback_path,
                 (long int)time(NULL)) >= PATH_MAX)
        SAYX(EXIT_FAILURE,"filename was truncated to %d bytes", PATH_MAX);
    recreate_fallback_path(worker->fallback_path);
    fd = open(worker->fallback_file, O_WRONLY|O_APPEND|O_CREAT, 0640);
    if (fd < 0)
        _ENO("failed to open '%s', everyting is lost!", worker->fallback_file);
    return fd;
}

static void write_blob_to_disk_locked(worker_t *worker, int fd, blob_t *b) {
    assert(BLOB_REF_PTR(b));
    if (write(fd, BLOB_BUF(b), BLOB_BUF_SIZE(b)) != BLOB_BUF_SIZE(b))
        _ENO("failed to write '%s', everyting is lost!", worker->fallback_file);
}

static void deal_with_failed_send_locked(worker_t *worker, struct queue *q) {
    blob_t *b;
    int fd= get_epoch_filehandle_locked(worker);
    for (b = q_shift_nolock(q); b != NULL; b = q_shift_nolock(q)) {
        write_blob_to_disk_locked(worker, fd, b);
        b_destroy(b);
    }
    if (fsync(fd))
        _ENO("failed to fsync '%s', everyting is lost!", worker->fallback_file);
    if (close(fd))
        _ENO("failed to close '%s', everyting is lost!", worker->fallback_file);
}

void *worker_thread(void *arg) {
    worker_t *self = (worker_t *) arg;
    struct queue hijacked_queue;
    struct queue *q = &self->queue;
    struct sock *s = &self->s_output;
    blob_t *b;
    memset(&hijacked_queue, 0, sizeof(hijacked_queue));

again:
    while(!RELAY_ATOMIC_READ(self->exit) && !open_socket(s, DO_CONNECT | DO_NOT_EXIT)) {
        w_wait(SLEEP_AFTER_DISASTER);
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
        cork(s,1);
        while ((b = hijacked_queue.head) != NULL) {
            if (SEND(s,b) < 0) {
                _ENO("ABORT: send to %s failed %ld",s->to_string, BLOB_DATA_MBR_SIZE(b));
                // race between destruction and this point, but worker_destroy
                // will pthread_join() us, so no issue
                deal_with_failed_send_locked(self, &hijacked_queue);
                close(s->socket);
                goto again;
            }
            b_destroy( q_shift_nolock( &hijacked_queue ) );
            self->sent++;
        }
        cork(s,0);
        w_wait(0);
    }
    close(s->socket);
    _D("worker[%s] sent %llu packets in its lifetime", s->to_string, self->sent);
    return NULL;
}

int enqueue_blob_for_transmission(blob_t *b) {
    int i = 0;
    worker_t *w;
    LOCK(&GIANT.lock);
    BLOB_REFCNT_set(b,GIANT.n_workers);
    TAILQ_FOREACH(w, &GIANT.workers, entries) {
        if (w == TAILQ_FIRST(&GIANT.workers)) {
            q_append_locked(w, b);
        } else {
            q_append_locked(w, b_clone_no_refcnt_inc(b));
        }
        i++;
    }
    UNLOCK(&GIANT.lock);
    w_wakeup();
    if (i == 0) {
        _E("no living workers, not sure what to do"); // dump the packet on disk?
    }
    return i;
}

void w_wakeup(void) {
    pthread_mutex_lock(&GIANT.cond_lock);
    pthread_cond_broadcast(&GIANT.cond);
    pthread_mutex_unlock(&GIANT.cond_lock);
}

void w_wait(int seconds) {
    pthread_mutex_lock(&GIANT.cond_lock);
    if (seconds > 0) {
        struct timeval    tp;
        struct timespec   ts;
        gettimeofday(&tp, NULL);
        ts.tv_sec  = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec * 1000;
        ts.tv_sec += seconds;
        pthread_cond_timedwait(&GIANT.cond, &GIANT.cond_lock,&ts);
    } else {
        pthread_cond_wait(&GIANT.cond, &GIANT.cond_lock);
    }
    pthread_mutex_unlock(&GIANT.cond_lock);
}

worker_t * worker_init_locked(char *arg) {
    worker_t *worker = malloc_or_die(sizeof(*worker));

    /* wipe worker */
    memset(worker,0,sizeof(*worker));

    /* setup flags */
    worker->exists = 1;
    worker->arg = strdup(arg);

    /* socketize */
    socketize(arg, &worker->s_output);

    /* setup fallback_path */
    if (snprintf(worker->fallback_path, PATH_MAX,FALLBACK_ROOT "/%s/", worker->s_output.to_string) >= PATH_MAX)
	SAYX(EXIT_FAILURE,"fallback_path too big, had to be truncated: %s", worker->fallback_path);
    recreate_fallback_path(worker->fallback_path);

    /* and finally create the thread */
    pthread_create(&worker->tid, NULL, worker_thread, worker);

    setproctitle("relay","worker");

    /* return the worker */
    return worker;
}

void worker_destroy_locked(worker_t *worker) {
    uint32_t old_exit= RELAY_ATOMIC_OR(worker->exit,EXIT_FLAG);

    if (old_exit & EXIT_FLAG)
        return;

    close(worker->s_output.socket);
    w_wakeup();
    pthread_join(worker->tid, NULL);
    deal_with_failed_send_locked(worker, &worker->queue);
    free(worker->arg);
    free(worker);
}

void worker_init_static(int argc, char **argv, int reload) {
    int i;
    int must_add;
    worker_t *w,*wtmp;
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
        int n_workers = 0;
        TAILQ_FOREACH_SAFE(w,&GIANT.workers,entries,wtmp) {
            if (w->exists == 0) {
                TAILQ_REMOVE(&GIANT.workers, w, entries);
                worker_destroy_locked(w);
            } else {
                n_workers++;
            }
        }
        GIANT.n_workers = n_workers;
        UNLOCK(&GIANT.lock);
    } else {
        TAILQ_INIT(&GIANT.workers);
        LOCK_INIT(&GIANT.lock);
        pthread_mutex_init(&GIANT.cond_lock, NULL);
        pthread_cond_init(&GIANT.cond, NULL);
        LOCK(&GIANT.lock);
        GIANT.n_workers = 0;
        for (i = 0; i < argc; i++) {
            w = worker_init_locked(argv[i]);
            TAILQ_INSERT_HEAD(&GIANT.workers, w, entries);
            GIANT.n_workers++;
        }
        UNLOCK(&GIANT.lock);
    }
}

void worker_destroy_static(void) {
    worker_t *w;
    LOCK(&GIANT.lock);
    while ((w = TAILQ_FIRST(&GIANT.workers)) != NULL) {
        TAILQ_REMOVE(&GIANT.workers, w, entries);
        worker_destroy_locked(w);
    }
    UNLOCK(&GIANT.lock);
}
