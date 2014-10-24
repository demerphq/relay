#ifndef RELAY_WORKER_H
#define RELAY_WORKER_H

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "blob.h"
#include "config.h"
#include "control.h"
#include "disk_writer.h"
#include "relay.h"
#include "relay_threads.h"
#include "setproctitle.h"
#include "socket_util.h"
#include "stats.h"
#include "timer.h"

/* a socket worker */
struct worker {
    queue_t queue;
    pthread_t tid;

    volatile uint32_t exit;

    stats_basic_counters_t counters;
    stats_basic_counters_t totals;

    config_t *config;

    sock_t s_output;
    char *arg;
    volatile uint32_t exists;
    disk_writer_t *disk_writer;

     TAILQ_ENTRY(worker) entries;
};
typedef struct worker worker_t;

/* worker.c */
worker_t *worker_init(char *arg, config_t * config);
void worker_destroy(worker_t * worker);
void *worker_thread(void *arg);

/* worker sleeps while it waits for work
 * XXX this should be configurable */
static INLINE void worker_wait_millisec(unsigned int millisec)
{
    /* XXX should be replaced by more standard nanosleep() */
    usleep(millisec * 1000);
}

#endif				/* #ifndef RELAY_WORKER_H */
