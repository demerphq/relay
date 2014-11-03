#ifndef RELAY_SOCKET_WORKER_POOL_H
#define RELAY_SOCKET_WORKER_POOL_H

#include "relay_common.h"
#include "graphite_worker.h"
#include "socket_worker.h"

/* this is our GIANT lock and state object. aint globals lovely. :-)*/
struct socket_worker_pool {
    /* macro to define a TAILQ head entry, empty first arg deliberate */
    TAILQ_HEAD(, socket_worker) workers;
    LOCK_T lock;
    int n_workers;

};
typedef struct socket_worker_pool socket_worker_pool_t;

void worker_pool_init_static(config_t * config);
void worker_pool_reload_static(config_t * config);
void worker_pool_destroy_static(void);
int enqueue_blob_for_transmission(blob_t * b);
void update_process_status(fixed_buffer_t * buf, stats_count_t received, stats_count_t tcp);

#endif				/* #ifndef RELAY_SOCKET_WORKER_POOL_H */
