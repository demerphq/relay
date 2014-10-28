#ifndef RELAY_GRAPHITE_WORKER_H
#define RELAY_GRAPHITE_WORKER_H

#include <pthread.h>

#include "socket_util.h"
#include "string_util.h"
#include "worker_base.h"

#define GRAPHITE_BUFFER_MAX 16384

struct graphite_worker {
    struct worker_base base;

    relay_socket_t s_output;

    char *root;

    fixed_buffer_t *buffer;
};

typedef struct graphite_worker graphite_worker_t;

graphite_worker_t *graphite_worker_create(const config_t * config);
void graphite_worker_destroy(graphite_worker_t * worker);
void *graphite_worker_thread(void *arg);

#endif				/* #ifndef RELAY_GRAPHITE_WORKER_H */
