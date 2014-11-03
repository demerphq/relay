#ifndef RELAY_GLOBAL_H
#define RELAY_GLOBAL_H

#include "config.h"
#include "graphite_worker.h"
#include "socket_worker_pool.h"

struct relay_global {
    volatile uint32_t control;
    volatile int exit_code;
    config_t *config;
    relay_socket_t *listener;
    graphite_worker_t *graphite_worker;
    socket_worker_pool_t pool;
};
typedef struct relay_global relay_global_t;

extern relay_global_t GLOBAL;

#endif				/* #ifndef RELAY_GLOBAL_H */
