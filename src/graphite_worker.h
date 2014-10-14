#ifndef RELAY_GRAPHITE_WORKER_H
#define RELAY_GRAPHITE_WORKER_H

#include <pthread.h>

#include "socket_util.h"

#define GRAPHITE_BUFFER_MAX 16384

struct graphite_worker {
    pthread_t tid;

    volatile uint32_t exit;

    sock_t s_output;

    char *arg;
    char *root;
    char *buffer;
};

typedef struct graphite_worker graphite_worker_t;
void graphite_worker_destroy(graphite_worker_t * worker);
void *graphite_worker_thread(void *arg);

#endif				/* #ifndef RELAY_GRAPHITE_WORKER_H */
