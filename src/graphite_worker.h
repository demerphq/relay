#ifndef _GRAPHITE_WORKER_H
#define _GRAPHITE_WORKER_H

struct graphite_worker {
    pthread_t tid;

    volatile uint32_t exit;

    sock_t s_output;

    char *arg;
};
typedef struct graphite_worker graphite_worker_t;

#endif
