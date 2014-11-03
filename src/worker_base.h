#ifndef RELAY_WORKER_BASE_H
#define RELAY_WORKER_BASE_H

#include <pthread.h>
#include <sys/types.h>

#include "config.h"

struct worker_base {
    pthread_t tid;

    const config_t *config;

    char *arg;

    /* If non-zero, this worker is already stopping. */
    volatile uint32_t stopping;
};

#endif				/* #ifndef RELAY_WORKER_BASE_H */
