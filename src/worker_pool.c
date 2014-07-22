#include "worker_pool.h"
static worker_pool_t POOL;

/* initialize a pool of workers
 */
void worker_pool_init_static(int argc, char **argv) {
    int i;
    worker_t *new_worker;

    TAILQ_INIT(&POOL.workers);
    LOCK_INIT(&POOL.lock);

    LOCK(&POOL.lock);
    POOL.n_workers = 0;
    for (i = 0; i < argc; i++) {
        if (is_aborted())
            break;
        new_worker = worker_init(argv[i]);
        TAILQ_INSERT_HEAD(&POOL.workers, new_worker, entries);
        POOL.n_workers++;
    }
    UNLOCK(&POOL.lock);
}

/* re-initialize a pool of workers
 */
void worker_pool_reload_static(int argc, char **argv) {
    int i;
    int must_add;
    worker_t *w;
    worker_t *wtmp;
    int n_workers = 0;

    LOCK(&POOL.lock);

    /* clear the exists bit of each worker */
    TAILQ_FOREACH(w, &POOL.workers, entries) {
        w->exists = 0;
    }

    /* scan through each argument, and see if we need
     * to add a new worker for it, or if we already have it */
    for (i = 0; i < argc; i++) {
        must_add = 1;
        TAILQ_FOREACH(w, &POOL.workers, entries) {
            if ( !w->exists && strcmp(argv[i], w->arg) == 0 ) {
                w->exists = 1;
                must_add = 0;
                break;
            }
        }
        if (must_add) {
            w = worker_init(argv[i]); /* w will have w->exists == 1 */
            TAILQ_INSERT_TAIL(&POOL.workers, w, entries);
        }
    }

    TAILQ_FOREACH_SAFE(w, &POOL.workers, entries, wtmp) {
        if (w->exists == 0) {
            TAILQ_REMOVE(&POOL.workers, w, entries);
            UNLOCK(&POOL.lock);

            worker_destroy(w); // might lock

            LOCK(&POOL.lock);
        } else {
            n_workers++;
        }
    }
    POOL.n_workers = n_workers;
    UNLOCK(&POOL.lock);
}

/* worker destory static, destroy all the workers in the pool */
void worker_pool_destroy_static(void) {
    worker_t *w;
    LOCK(&POOL.lock);
    while ((w = TAILQ_FIRST(&POOL.workers)) != NULL) {
        TAILQ_REMOVE(&POOL.workers, w, entries);
        UNLOCK(&POOL.lock);

        worker_destroy(w); // might lock

        LOCK(&POOL.lock);
    }
    UNLOCK(&POOL.lock);
}


