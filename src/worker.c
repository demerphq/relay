#include "relay.h"
int workers_count = 0;
static struct worker *WORKERS[MAX_WORKERS + 1];
#define FALLBACK WORKERS[MAX_WORKERS]

static void disaster_someone_else_try(struct worker *worker, struct queue *q);
static INLINE void cork(struct sock *s,int flag);

int q_append(struct worker *worker, blob_t *b) {
    struct queue *q = &worker->queue;

    LOCK(&q->lock);
    
    if (q->head == NULL)
        q->head = b;
    else
        q->tail->next = b;
    q->tail = b;
    b->next = NULL;
    q->count++;
    worker_signal(worker);

    UNLOCK(&q->lock);
    return 1;
}

blob_t *q_shift_nolock(struct queue *q) {
    blob_t *b= q->head;
    if (b) {
        if (b->next)
            q->head = b->next;
        else
            q->head = q->tail = NULL;
        q->count--;
    }
}

blob_t *q_shift_lock(struct queue *q) {
    blob_t *b;
    LOCK(&q->lock);
    b= q_shift_nolock(q);
    UNLOCK(&q>lock);
    return b;
}

static void write_blob_to_disk(struct worker *worker, blob_t *b) {
   /* XXX: Not Yet Implemented */
}

static void deal_with_failed_send(struct worker *worker, struct queue *q) {
    blob_t *b;
    for (b = q_shift_nolock(q); b != NULL; b = q_shift_nolock(q)) {
        write_blob_to_disk(worker, b);
        b_destroy(b);
    }
}

void *worker_thread(void *arg) {
    struct worker *self = (struct worker *) arg;
    struct queue hijacked_queue;
    struct queue *q = &self->queue;
    struct sock *s = &self->s_output;
    blob_t *b;
    bzero(&hijacked_queue,sizeof(hijacked_queue));

again:
    while(    self->abort
          && !self->exit
          && !open_socket(s,DO_CONNECT | DO_NOT_EXIT)) {

        worker_wait(self,SLEEP_AFTER_DISASTER);
    }

    self->abort = 0;

    while(!self->abort && !self->exit) {
        /* hijack the queue - copy the queue state into our private copy
         * and then reset the queue state to empty. So the formerly
         * shared queue is now private. We only do this if necessary. */
        if (hijacked_queue->head == NULL) {
            LOCK(&q->lock);
            memcpy(&hijacked_queue, q, sizeof(struct queue));
            q->tail = q->head = NULL;
            q->count = 0;
            UNLOCK(&q->lock);
        }
        cork(s,1);
        while ((b = hijacked_queue.head) != NULL) {
            if (SEND(s,b) < 0) {
                _ENO("ABORT: send to %s failed %d",socket_to_string(s),b->size);

                self->abort = 1;

                deal_with_failed_send(self, hijacked_queue);
                goto again;
            }
            b_destroy( q_shift_nolock( &hijacked_queue ) );
            self->sent++;
            // _TD("worker[%s] sent %llu packets",socket_to_string(s),self->sent);
        }
        cork(s,0);
        worker_wait(self,0);
    }
    close(s->socket);
    _D("worker[%s] sent %llu packets in its lifetime",socket_to_string(s),self->sent);
    return NULL;
}



int enqueue_blob_for_transmission(blob_t *b) {
    int i;
    blob_t *append_b;
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
    worker->abort = 1;

    socketize(arg,&worker->s_output);
    pthread_mutex_init(&worker->cond_lock, NULL);
    LOCK_INIT(&worker->queue.lock);
    pthread_cond_init(&worker->cond,NULL);
    pthread_create(&worker->tid,NULL,worker_thread,worker);

    worker->queue.limit = MAX_QUEUE_SIZE;
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

void worker_init_static(int ac, char **av,int destroy) {
    if (destroy)
        worker_destroy_static();

    bzero(WORKERS,sizeof(WORKERS));

    FALLBACK=worker_init(av[0]);
    FALLBACK->queue.limit = 0;
    ac--;
    av++;
    if (ac > MAX_WORKERS)
        _E("destination hosts(%d) > max workers(%d) %d will be skipped",ac,MAX_WORKERS,ac - MAX_WORKERS);
    for (workers_count = 0;workers_count < MAX_WORKERS; workers_count++)
        WORKERS[workers_count] = worker_init(av[(workers_count % ac)]);

}

void worker_destroy_static(void) {
    int i;
    for (i = 0;i < MAX_WORKERS + 1; i++)
        worker_destroy(WORKERS[i]);
}


static INLINE void cork(struct sock *s,int flag) {
    if (s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        _TE("setsockopt: %s",strerror(errno));
}
