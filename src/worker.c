#include "relay.h"
#include "worker.h"
int workers_count = 0;
static struct worker *WORKERS[MAX_WORKERS + 1];
#ifdef TCP_CORK
static INLINE void cork(struct sock *s,int flag) {
    if (s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        _ENO("setsockopt: %s",strerror(errno));
}
#else
#define cork(a,b)
#endif

int q_append(struct worker *worker, blob_t *b) {
    struct queue *q = &worker->queue;

    LOCK(&q->lock);

    if (q->head == NULL)
        q->head = b;
    else
        BLOB_NEXT_set(q->tail,b);
    q->tail = b;
    BLOB_NEXT_set(b,NULL);
    q->count++;
    worker_signal(worker);

    UNLOCK(&q->lock);
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

blob_t *q_shift_lock(struct queue *q) {
    blob_t *b;
    LOCK(&q->lock);
    b= q_shift_nolock(q);
    UNLOCK(&q->lock);
    return b;
}

static void recreate_fallback_path(char *dir) {
    if (mkdir(dir,0750) == -1 && errno != EEXIST)
        SAYX(EXIT_FAILURE,"mkdir of %s failed",dir);
}

int get_epoch_filehandle(struct worker *worker) {
    int fd;
    if (snprintf(worker->fallback_file, PATH_MAX, "%s/%li.srlc",
                 worker->fallback_path,
                 (long int)time(NULL)) >= PATH_MAX)
        SAYX(EXIT_FAILURE,"filename was truncated to %d bytes", PATH_MAX);
    recreate_fallback_path(worker->fallback_path);
    fd = open(worker->fallback_file,O_WRONLY|O_APPEND|O_CREAT,0640);
    if (fd < 0)
        _D("failed to open '%s', everyting is lost!: %s",worker->fallback_file,strerror(errno)); /* show reason? */
    return fd;
}

static void write_blob_to_disk(struct worker *worker, int fd, blob_t *b) {
    if (!BLOB_REF_PTR(b))
        SAYX(EXIT_FAILURE,"BLOB_REF_PTR(b) is null"); /* XXX */
    if (!BLOB_DATA_PTR(b))
        SAYX(EXIT_FAILURE,"BLOB_DATA_PTR(b) is null"); /* XXX */
    if (write(fd,BLOB_DATA(b),BLOB_SIZE(b)) != BLOB_SIZE(b))
        _D("failed to append to '%s', everything is lost!: %s", worker->fallback_file,strerror(errno));
}

static void deal_with_failed_send(struct worker *worker, struct queue *q) {
    blob_t *b;
    int fd= get_epoch_filehandle(worker);
    for (b = q_shift_nolock(q); b != NULL; b = q_shift_nolock(q)) {
        write_blob_to_disk(worker, fd, b);
        b_destroy(b);
    }
    if (fsync(fd))
        _D("failed to fsync '%s', everything is lost: %s", worker->fallback_file,strerror(errno));
    if (close(fd))
        _D("failed to close '%s', everything is lost: %s", worker->fallback_file,strerror(errno));
}

void *worker_thread(void *arg) {
    struct worker *self = (struct worker *) arg;
    struct queue hijacked_queue;
    struct queue *q = &self->queue;
    struct sock *s = &self->s_output;
    blob_t *b;
    bzero(&hijacked_queue,sizeof(hijacked_queue));

again:
    while(!self->exit && !open_socket(s,DO_CONNECT | DO_NOT_EXIT)) {
        worker_wait(self,SLEEP_AFTER_DISASTER);
    }

    while(!self->exit) {
        /* hijack the queue - copy the queue state into our private copy
         * and then reset the queue state to empty. So the formerly
         * shared queue is now private. We only do this if necessary. */
        if (hijacked_queue.head == NULL) {
            LOCK(&q->lock);
            memcpy(&hijacked_queue, q, sizeof(struct queue));
            q->tail = q->head = NULL;
            q->count = 0;
            UNLOCK(&q->lock);
        }
        cork(s,1);
        while ((b = hijacked_queue.head) != NULL) {
            if (SEND(s,b) < 0) {
                _ENO("ABORT: send to %s failed %ld",s->to_string,BLOB_DATA_SIZE(b));

                deal_with_failed_send(self, &hijacked_queue);
                close(s->socket);
                goto again;
            }
            b_destroy( q_shift_nolock( &hijacked_queue ) );
            self->sent++;
        }
        cork(s,0);
        worker_wait(self,0);
    }
    close(s->socket);
    _D("worker[%s] sent %llu packets in its lifetime",s->to_string,self->sent);
    return NULL;
}

int enqueue_blob_for_transmission(blob_t *b) {
    int i;
    blob_t *append_b;
    BLOB_REFCNT(b)= workers_count;
    for (i = 0; i < workers_count; i++) {
        if (i + 1 == workers_count) {
            append_b= b_clone(b);
        } else {
            append_b= b;
        }
        q_append(WORKERS[i],append_b);
    }
    return workers_count;
}

void worker_signal(struct worker *worker) {
    pthread_mutex_lock(&worker->cond_lock);
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->cond_lock);
}

void worker_wait(struct worker *worker, int seconds) {
    pthread_mutex_lock(&worker->cond_lock);
    if (seconds > 0) {
        struct timeval    tp;
        struct timespec   ts;
        gettimeofday(&tp, NULL);
        ts.tv_sec  = tp.tv_sec;
        ts.tv_nsec = tp.tv_usec * 1000;
        ts.tv_sec += seconds;
        pthread_cond_timedwait(&worker->cond,&worker->cond_lock,&ts);
    } else {
        pthread_cond_wait(&worker->cond,&worker->cond_lock);
    }
    pthread_mutex_unlock(&worker->cond_lock);
}

struct worker * worker_init(char *arg) {
    struct worker *worker = malloc_or_die(sizeof(*worker));
    bzero(worker,sizeof(*worker));
    worker->exit = 0;
    socketize(arg,&worker->s_output);
    pthread_mutex_init(&worker->cond_lock, NULL);
    LOCK_INIT(&worker->queue.lock);
    pthread_cond_init(&worker->cond,NULL);
    pthread_create(&worker->tid,NULL,worker_thread,worker);
    if (snprintf(worker->fallback_path,PATH_MAX,FALLBACK_ROOT "/%s/",worker->s_output.to_string) >= PATH_MAX)
	SAYX(EXIT_FAILURE,"fallback_path too big, had to be truncated: %s",worker->fallback_path);
    recreate_fallback_path(worker->fallback_path);
    worker->queue.count = 0;
    return worker;
}

void worker_destroy(struct worker *worker) {
    if (!worker->exit) {
        worker->exit = 1;
        worker_signal(worker);
        pthread_join(worker->tid,NULL);
    }
    LOCK_DESTROY(&worker->queue.lock);
    pthread_mutex_destroy(&worker->cond_lock);
    pthread_cond_destroy(&worker->cond);
    free(worker);
}

void worker_init_static(int argc, char **argv, int destroy) {
    if (destroy)
        worker_destroy_static();

    bzero(WORKERS,sizeof(WORKERS));

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
