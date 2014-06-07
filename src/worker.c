#include "relay.h"
#include "worker.h"
int workers_count = 0;
static worker_t *WORKERS[MAX_WORKERS + 1];
#ifdef TCP_CORK
static INLINE void cork(sock_t *s, int flag) {
    if (s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        _ENO("setsockopt: %s", strerror(errno));
}
#else
#define cork(a, b)
#endif

int q_append(worker_t *worker, blob_t *b) {
    queue_t *q = &worker->queue;

    LOCK(&q->lock);

    if (q->head == NULL)
        q->head = b;
    else
        BLOB_NEXT_set(q->tail, b);
    q->tail = b;
    BLOB_NEXT_set(b, NULL);
    q->count++;
    worker_signal(worker);

    UNLOCK(&q->lock);
    return 1;
}

blob_t *q_shift_nolock(queue_t *q) {
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

blob_t *q_shift_lock(queue_t *q) {
    blob_t *b;
    LOCK(&q->lock);
    b= q_shift_nolock(q);
    UNLOCK(&q->lock);
    return b;
}

static void recreate_fallback_path(char *dir) {
    if (mkdir(dir, 0750) == -1 && errno != EEXIST)
        SAYX(EXIT_FAILURE, "mkdir of %s failed", dir);
}

int get_epoch_filehandle(worker_t *worker) {
    int fd;
    if (snprintf(worker->fallback_file, PATH_MAX, "%s/%li.srlc",
                 worker->fallback_path,
                 (long int)time(NULL)) >= PATH_MAX)
        SAYX(EXIT_FAILURE, "filename was truncated to %d bytes", PATH_MAX);
    recreate_fallback_path(worker->fallback_path);
    fd = open(worker->fallback_file, O_WRONLY|O_APPEND|O_CREAT, 0640);
    if (fd < 0)
        _D("failed to open '%s', everyting is lost!: %s", worker->fallback_file, strerror(errno)); /* show reason? */
    return fd;
}

static void write_blob_to_disk(worker_t *worker, int fd, blob_t *b) {
    assert(BLOB_REF_PTR(b));
    if (write(fd, BLOB_BUF(b), BLOB_BUF_SIZE(b)) != BLOB_BUF_SIZE(b))
        _D("failed to append to '%s', everything is lost!: %s", worker->fallback_file, strerror(errno));
}

static void deal_with_failed_send(worker_t *worker, queue_t *q) {
    blob_t *b;
    int fd= get_epoch_filehandle(worker);
    for (b = q_shift_nolock(q); b != NULL; b = q_shift_nolock(q)) {
        write_blob_to_disk(worker, fd, b);
        b_destroy(b);
    }
    if (fsync(fd))
        _D("failed to fsync '%s', everything is lost: %s", worker->fallback_file, strerror(errno));
    if (close(fd))
        _D("failed to close '%s', everything is lost: %s", worker->fallback_file, strerror(errno));
}

INLINE void hijack_queue (worker_t *self, queue_t *hijacked_queue)
{
    queue_t *q = &self->queue;
    LOCK(&q->lock);
    memcpy(hijacked_queue, q, sizeof(queue_t));
    q->tail = q->head = NULL;
    q->count = 0;
    UNLOCK(&q->lock);
}

void *worker_thread(void *arg) {
    worker_t *self = (worker_t *) arg;
    queue_t hijacked_queue;
    sock_t *s = &self->s_output;
    blob_t *b;
    memset(&hijacked_queue, 0, sizeof(hijacked_queue));

again:
    while(!self->exit && !open_socket(s, DO_CONNECT | DO_NOT_EXIT)) {
        worker_wait(self, SLEEP_AFTER_DISASTER);
    }

    while(!self->exit) {
        /* hijack the queue - copy the queue state into our private copy
         * and then reset the queue state to empty. So the formerly
         * shared queue is now private. We only do this if necessary. */
        if (hijacked_queue.head == NULL) {
            hijack_queue(self, &hijacked_queue);
        }
        if (hijacked_queue.head) {
            cork(s, 1);
            while ((b = hijacked_queue.head) != NULL) {
                if (SEND(s, b) < 0) {
                    _ENO("ABORT: send to %s failed %ld", s->to_string, BLOB_DATA_MBR_SIZE(b));

                    deal_with_failed_send(self, &hijacked_queue);
                    close(s->socket);
                    goto again;
                }
                b_destroy( q_shift_nolock( &hijacked_queue ) );
                self->sent++;
            }
            cork(s, 0);
        }
        worker_wait(self, 0);
    }
    close(s->socket);
    _D("worker[%s] sent %llu packets in its lifetime", s->to_string, self->sent);
    return NULL;
}

int enqueue_blob_for_transmission(blob_t *b) {
    int i;
    blob_t *append_b;
    /* we overwrite the recount here with the number of workers we
     * are going to issue this item to, that way even if they process
     * the item faster than we can allocate it (which is unlikely anyway),
     * when they call destroy it wont hit 0 until all the workers have
     * processed it. */
    BLOB_REFCNT_set(b, workers_count);
    for (i = 0; i < workers_count; i++) {
        // send the original blob to the last worker
        if (i + 1 == workers_count) {
            append_b= b;
        } else {
            append_b= b_clone_no_refcnt_inc(b);
        }
        q_append(WORKERS[i], append_b);
    }
    return workers_count;
}

void worker_signal(worker_t *worker) {
    pthread_mutex_lock(&worker->cond_lock);
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->cond_lock);
}

void worker_wait(worker_t *worker, int seconds) {
    pthread_mutex_lock(&worker->cond_lock);
    if (seconds > 0) {
        struct timeval    tp;
        struct timespec   ts;
        gettimeofday(&tp, NULL);
        ts.tv_sec  = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec * 1000;
        ts.tv_sec += seconds;
        pthread_cond_timedwait(&worker->cond, &worker->cond_lock, &ts);
    } else {
        pthread_cond_wait(&worker->cond, &worker->cond_lock);
    }
    pthread_mutex_unlock(&worker->cond_lock);
}

worker_t * worker_init(char *arg) {
    worker_t *worker = malloc_or_die(sizeof(*worker));
    memset(worker, 0, sizeof(*worker));
    worker->exit = 0;
    worker->queue.count = 0;
    socketize(arg, &worker->s_output);
    pthread_mutex_init(&worker->cond_lock, NULL);
    LOCK_INIT(&worker->queue.lock);
    pthread_cond_init(&worker->cond, NULL);
    pthread_create(&worker->tid, NULL, worker_thread, worker);
    if (snprintf(worker->fallback_path, PATH_MAX, FALLBACK_ROOT "/%s/", worker->s_output.to_string) >= PATH_MAX)
        SAYX(EXIT_FAILURE, "fallback_path too big, had to be truncated: %s", worker->fallback_path);
    recreate_fallback_path(worker->fallback_path);
    return worker;
}

void worker_destroy(worker_t *worker) {
    if (!worker->exit) {
        worker->exit = 1;
        worker_signal(worker);
        pthread_join(worker->tid, NULL);
    }
    deal_with_failed_send(worker, &worker->queue);
    LOCK_DESTROY(&worker->queue.lock);
    pthread_mutex_destroy(&worker->cond_lock);
    pthread_cond_destroy(&worker->cond);
    free(worker);
}

void worker_init_static(int argc, char **argv, int destroy) {
    if (destroy)
        worker_destroy_static();

    memset(WORKERS, 0, sizeof(WORKERS));

    if (argc > MAX_WORKERS)
        _D("destination hosts(%d) > max workers(%d)", argc, MAX_WORKERS);

    for (workers_count = 0; workers_count < argc; workers_count++)
        WORKERS[workers_count] = worker_init(argv[workers_count]);

}

void worker_destroy_static(void) {
    int i;
    for (i = 0;i < workers_count; i++)
        worker_destroy(WORKERS[i]);
}
