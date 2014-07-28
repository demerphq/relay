#ifndef _WORKER_POOL_H
#define _WORKER_POOL_H

#include "relay_common.h"
#include "graphite_worker.h"
#include "worker.h"

/* this is our GIANT lock and state object. aint globals lovely. :-)*/
struct worker_pool {
    /* macro to define a TAILQ head entry, empty first arg deliberate */
    TAILQ_HEAD(, worker) workers;
    LOCK_T lock;
    int n_workers;

    graphite_worker_t *graphite_worker;
};
typedef struct worker_pool worker_pool_t;

void worker_pool_init_static(config_t *config);
void worker_pool_reload_static(config_t *config);
void worker_pool_destroy_static(void);
int enqueue_blob_for_transmission(blob_t *b);
void add_worker_stats_to_ps_str(char *str, ssize_t len);

#endif
