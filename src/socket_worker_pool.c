#include "socket_worker_pool.h"

#include "global.h"
#include "log.h"
#include "relay_threads.h"
#include "setproctitle.h"
#include "string_util.h"

/* update the process status line with the status of the workers */
void update_process_status(fixed_buffer_t * buf, config_t * config, stats_count_t received, stats_count_t tcp)
{
    LOCK(&GLOBAL.pool.lock);
    fixed_buffer_reset(buf);
    do {
        for (int i = 0; i < config->argc; i++) {
            if (!fixed_buffer_vcatf(buf, "%s ", config->argv[i]))
                break;
        }
        if (fixed_buffer_vcatf(buf, " : received %lu tcp %lu", (unsigned long) received, (unsigned long) tcp)) {
            socket_worker_t *w;
            int worker_id = 0;
            TAILQ_FOREACH(w, &GLOBAL.pool.workers, entries) {
                /* This is so ugly. */
                if (!fixed_buffer_vcatf(buf,
                                        " [%d] freq %.1fs ",
                                        ++worker_id, (double) config->graphite.send_interval_millisec / 1000))
                    break;
                if (!fixed_buffer_vcatf(buf,
                                        "received %lu/%lu ",
                                        (unsigned long) RELAY_ATOMIC_READ(w->recents.received_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->totals.received_count)))
                    break;
                if (!fixed_buffer_vcatf(buf,
                                        "%d:%d:%d ",
                                        (int) w->rates[0].received.rate,
                                        (int) w->rates[1].received.rate, (int) w->rates[2].received.rate))
                    break;

                if (!fixed_buffer_vcatf(buf,
                                        "sent %lu/%lu ",
                                        (unsigned long) RELAY_ATOMIC_READ(w->recents.sent_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->totals.sent_count)))
                    break;
                if (!fixed_buffer_vcatf(buf,
                                        "%d:%d:%d ",
                                        (int) w->rates[0].sent.rate,
                                        (int) w->rates[1].sent.rate, (int) w->rates[2].sent.rate))
                    break;

                if (!fixed_buffer_vcatf(buf,
                                        "spilled %lu/%lu ",
                                        (unsigned long) RELAY_ATOMIC_READ(w->recents.spilled_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->totals.spilled_count)))
                    break;
                if (!fixed_buffer_vcatf(buf,
                                        "%d:%d:%d ",
                                        (int) w->rates[0].spilled.rate,
                                        (int) w->rates[1].spilled.rate, (int) w->rates[2].spilled.rate))
                    break;

                if (!fixed_buffer_vcatf(buf,
                                        "dropped %lu/%lu ",
                                        (unsigned long) RELAY_ATOMIC_READ(w->recents.dropped_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->totals.dropped_count)))
                    break;
                if (!fixed_buffer_vcatf(buf,
                                        "%d:%d:%d ",
                                        (int) w->rates[0].dropped.rate,
                                        (int) w->rates[1].dropped.rate, (int) w->rates[2].dropped.rate))
                    break;

                if (!fixed_buffer_vcatf(buf,
                                        "disk %lu/%lu disk_error %lu/%lu",
                                        (unsigned long) RELAY_ATOMIC_READ(w->recents.disk_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->totals.disk_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->recents.disk_error_count),
                                        (unsigned long) RELAY_ATOMIC_READ(w->totals.disk_error_count)))
                    break;
            }
        }
        if (!fixed_buffer_vcatf
            (buf, " : blob active %ld bytes %ld refcnt_bytes %ld total %ld bytes %ld refcnt_bytes %ld",
             RELAY_ATOMIC_READ(GLOBAL.blob_active_count), RELAY_ATOMIC_READ(GLOBAL.blob_active_bytes),
             RELAY_ATOMIC_READ(GLOBAL.blob_active_refcnt_bytes), RELAY_ATOMIC_READ(GLOBAL.blob_total_count),
             RELAY_ATOMIC_READ(GLOBAL.blob_total_bytes), RELAY_ATOMIC_READ(GLOBAL.blob_total_refcnt_bytes))) {
            break;
        }
        {
            int64_t buckets = RELAY_ATOMIC_READ(GLOBAL.blob_total_ored_buckets);
            if (buckets) {
                if (!fixed_buffer_vcatf(buf, " buckets:", buckets)) {
                    break;
                }
                for (int i = 0;
                     buckets && i < (int) sizeof(GLOBAL.blob_total_sizes) / (int) sizeof(GLOBAL.blob_total_sizes[0]);
                     i++, buckets >>= 1) {
                    int64_t count = RELAY_ATOMIC_READ(GLOBAL.blob_total_sizes[i]);
                    if (count > 0 && !fixed_buffer_vcatf(buf, " %d:%ld", i, count)) {
                        break;
                    }
                }
            }
        }
    } while (0);
    UNLOCK(&GLOBAL.pool.lock);
    fixed_buffer_zero_terminate(buf);
    setproctitle(buf->data);
}

/* add an item to all workers queues
 * (not sure if this really belongs in worker.c)
 */
int enqueue_blob_for_transmission(blob_t * b)
{
    int i = 0;
    socket_worker_t *w;
    blob_t *to_enqueue;
    LOCK(&GLOBAL.pool.lock);
    BLOB_REFCNT_set(b, GLOBAL.pool.n_workers);
    TAILQ_FOREACH(w, &GLOBAL.pool.workers, entries) {
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
    UNLOCK(&GLOBAL.pool.lock);
    if (i == 0) {
        /* TODO dump the packet on disk? */
        WARN("no living workers, not sure what to do");
        blob_destroy(b);
    }
    return i;
}

/* initialize a pool of workers
 */
void worker_pool_init_static(config_t * config)
{
    socket_worker_t *new_worker;
    TAILQ_INIT(&GLOBAL.pool.workers);
    LOCK_INIT(&GLOBAL.pool.lock);
    LOCK(&GLOBAL.pool.lock);
    GLOBAL.pool.n_workers = 0;
    GLOBAL.pool.n_connected = 0;
    for (int i = 1; i < config->argc; i++) {
        if (control_is(RELAY_STOPPING))
            break;
        new_worker = socket_worker_create(config->argv[i], config);
        TAILQ_INSERT_HEAD(&GLOBAL.pool.workers, new_worker, entries);
        GLOBAL.pool.n_workers++;
    }
    UNLOCK(&GLOBAL.pool.lock);
}

/* re-initialize a pool of workers
 */
void worker_pool_reload_static(config_t * config)
{
    int must_add = 0;
    socket_worker_t *w;
    socket_worker_t *wtmp;
    int n_workers = 0;
    LOCK(&GLOBAL.pool.lock);
    /* clear the exists bit of each worker */
    TAILQ_FOREACH(w, &GLOBAL.pool.workers, entries) {
        w->exists = 0;
    }

    /* scan through each argument, and see if we need
     * to add a new worker for it, or if we already have it */
    for (int i = 1; i < config->argc; i++) {
        must_add = 1;
        TAILQ_FOREACH(w, &GLOBAL.pool.workers, entries) {
            if (!w->exists && STREQ(config->argv[i], w->base.arg)) {
                w->exists = 1;
                must_add = 0;
                break;
            }
        }
        if (must_add) {
            w = socket_worker_create(config->argv[i], config);  /* w will have w->exists == 1 */
            TAILQ_INSERT_TAIL(&GLOBAL.pool.workers, w, entries);
        }
    }

    TAILQ_FOREACH_SAFE(w, &GLOBAL.pool.workers, entries, wtmp) {
        if (w->exists == 0) {
            TAILQ_REMOVE(&GLOBAL.pool.workers, w, entries);
            UNLOCK(&GLOBAL.pool.lock);
            socket_worker_destroy(w);   /*  might lock */
            LOCK(&GLOBAL.pool.lock);
        } else {
            n_workers++;
        }
    }
    GLOBAL.pool.n_workers = n_workers;
    UNLOCK(&GLOBAL.pool.lock);
}

/* worker destory static, destroy all the workers in the pool */
void worker_pool_destroy_static(void)
{
    socket_worker_t *w;
    LOCK(&GLOBAL.pool.lock);
    while ((w = TAILQ_FIRST(&GLOBAL.pool.workers)) != NULL) {
        TAILQ_REMOVE(&GLOBAL.pool.workers, w, entries);
        UNLOCK(&GLOBAL.pool.lock);
        socket_worker_destroy(w);       /*  might lock */
        LOCK(&GLOBAL.pool.lock);
    }
    UNLOCK(&GLOBAL.pool.lock);
}
