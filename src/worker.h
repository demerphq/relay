#ifndef _WORKER_H
#define _WORKER_H

#include "relay_threads.h"
#include "blob.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#ifndef SLEEP_AFTER_DISASTER
#define SLEEP_AFTER_DISASTER 1
#endif

#define FALLBACK_ROOT "/tmp"

#ifndef MAX_WORKERS
#define MAX_WORKERS 255
#endif

struct queue {
    blob_t *head;
    blob_t *tail;
    LOCK_T lock;
    unsigned int count;
};
typedef struct queue queue_t;

struct worker {
    struct queue queue;
    pthread_mutex_t cond_lock;
    pthread_cond_t cond;
    pthread_t tid;
    unsigned long long sent;
    volatile int abort;
    volatile int exit;

    sock_t s_output;
    char fallback_path[PATH_MAX];
    char fallback_file[PATH_MAX];
};
typedef struct worker worker_t;

/* worker.c */
int enqueue_blob_for_transmission(blob_t *b);
void worker_destroy_static(void);
void worker_init_static(int ac, char **av,int destroy);

worker_t * worker_init(char *arg);
void worker_destroy(worker_t *worker);
INLINE void worker_signal(worker_t *worker);
INLINE void worker_wait(worker_t *worker,int delay);
void *worker_thread(void *arg);


#define SEND(S,B) (                                                     \
    ((S)->type != SOCK_DGRAM)                                           \
    ? send((S)->socket,BLOB_DATA_PTR(B), BLOB_DATA_SIZE(B), MSG_NOSIGNAL) \
    : sendto((S)->socket,BLOB_DATA(B), BLOB_SIZE(B), MSG_NOSIGNAL, \
             (struct sockaddr*) &(S)->sa.in,(S)->addrlen)               \
)


#endif
