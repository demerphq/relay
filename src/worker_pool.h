#ifndef _WORKER_POOL_H
#define _WORKER_POOL_H
/* this is our GIANT lock and state object. aint globals lovely. :-)*/
struct worker_pool {
    /* macro to define a TAILQ head entry, empty first arg deliberate */
    TAILQ_HEAD(, worker) workers;
    LOCK_T lock;
    int n_workers;
};
typedef struct worker_pool worker_pool_t;

void worker_pool_init_static(int argc, char **argv);
void worker_pool_reload_static(int argc, char **argv);
void worker_pool_destroy_static(void);

#endif
