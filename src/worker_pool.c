#include "worker_pool.h"

worker_pool_t POOL;

#define PROCESS_STATUS_BUF_LEN 128

/* update the process status line with the status of the workers */
void update_process_status(stats_count_t received, stats_count_t active)
{
    char str[PROCESS_STATUS_BUF_LEN + 1], *buf = str;
    int len = PROCESS_STATUS_BUF_LEN;
    worker_t *w;
    int worker_id = 0;
    int wrote_len = 0;

    LOCK(&POOL.lock);
    wrote_len = snprintf(buf, len, "received %lu active %lu", received, active);
    if (wrote_len > 0 && wrote_len < len) {
	buf += wrote_len;
	len -= wrote_len;
	TAILQ_FOREACH(w, &POOL.workers, entries) {
	    if (len <= 0)
		break;
	    wrote_len =
		snprintf(buf, len,
			 " %d: sent %lu spilled "
			 "%lu disk %lu disk_error %lu",
			 ++worker_id,
			 RELAY_ATOMIC_READ(w->totals.sent_count),
			 RELAY_ATOMIC_READ(w->totals.spilled_count),
			 RELAY_ATOMIC_READ(w->totals.disk_count), RELAY_ATOMIC_READ(w->totals.disk_error_count));
	    if (wrote_len <= 0 || wrote_len >= len)
		break;
	    buf += wrote_len;
	    len -= wrote_len;
	}
    }
    UNLOCK(&POOL.lock);
    setproctitle(str);
}

/* add an item to all workers queues
 * (not sure if this really belongs in worker.c)
 */
int enqueue_blob_for_transmission(blob_t * b)
{
    int i = 0;
    worker_t *w;
    blob_t *to_enqueue;
    LOCK(&POOL.lock);
    BLOB_REFCNT_set(b, POOL.n_workers);
    TAILQ_FOREACH(w, &POOL.workers, entries) {
	/* check if this item is no the last */
	if (TAILQ_NEXT(w, entries) == NULL) {
	    /* this is the last item in the chain, just use b. */
	    to_enqueue = b;
	} else {
	    /* not the last, so we need to clone the original object */
	    to_enqueue = blob_clone_no_refcnt_inc(b);
	}
	queue_append_nolock(&w->queue, to_enqueue);

	i++;
    }
    UNLOCK(&POOL.lock);
    if (i == 0) {
	WARN("no living workers, not sure what to do");	// dump the packet on disk?
	blob_destroy(b);
    }
    return i;
}

/* initialize a pool of workers
 */
void worker_pool_init_static(config_t * config)
{
    int i;
    worker_t *new_worker;

    TAILQ_INIT(&POOL.workers);
    LOCK_INIT(&POOL.lock);


    LOCK(&POOL.lock);
    POOL.n_workers = 0;
    for (i = 1; i < config->argc; i++) {
	if (is_stopped())
	    break;
	new_worker = worker_init(config->argv[i]);
	TAILQ_INSERT_HEAD(&POOL.workers, new_worker, entries);
	POOL.n_workers++;
    }
    UNLOCK(&POOL.lock);
}

/* re-initialize a pool of workers
 */
void worker_pool_reload_static(config_t * config)
{
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
    for (i = 1; i < config->argc; i++) {
	must_add = 1;
	TAILQ_FOREACH(w, &POOL.workers, entries) {
	    if (!w->exists && strcmp(config->argv[i], w->arg) == 0) {
		w->exists = 1;
		must_add = 0;
		break;
	    }
	}
	if (must_add) {
	    w = worker_init(config->argv[i]);	/* w will have w->exists == 1 */
	    TAILQ_INSERT_TAIL(&POOL.workers, w, entries);
	}
    }

    TAILQ_FOREACH_SAFE(w, &POOL.workers, entries, wtmp) {
	if (w->exists == 0) {
	    TAILQ_REMOVE(&POOL.workers, w, entries);
	    UNLOCK(&POOL.lock);

	    worker_destroy(w);	// might lock

	    LOCK(&POOL.lock);
	} else {
	    n_workers++;
	}
    }
    POOL.n_workers = n_workers;
    UNLOCK(&POOL.lock);
}

/* worker destory static, destroy all the workers in the pool */
void worker_pool_destroy_static(void)
{
    worker_t *w;
    LOCK(&POOL.lock);
    while ((w = TAILQ_FIRST(&POOL.workers)) != NULL) {
	TAILQ_REMOVE(&POOL.workers, w, entries);
	UNLOCK(&POOL.lock);

	worker_destroy(w);	// might lock

	LOCK(&POOL.lock);
    }
    UNLOCK(&POOL.lock);
}
