#ifndef RELAY_WORKER_H
#define RELAY_WORKER_H

#include "relay.h"
#include "setproctitle.h"
#include "relay_threads.h"
#include "blob.h"
#include "control.h"
#include "stats.h"
#include "config.h"
#include "timer.h"
#include "socket_util.h"
#include "disk_writer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

/* a socket worker */
struct worker {
    queue_t queue;
    pthread_t tid;

    volatile uint32_t exit;

    stats_basic_counters_t counters;
    stats_basic_counters_t totals;

    sock_t s_output;
    char *arg;
    volatile uint32_t exists;
    disk_writer_t *disk_writer;

     TAILQ_ENTRY(worker) entries;
};
typedef struct worker worker_t;

/* worker.c */
worker_t *worker_init(char *arg);
void worker_destroy(worker_t * worker);
void *worker_thread(void *arg);

/* worker sleeps while it waits for work
 * this should be configurable */
INLINE static void worker_wait(unsigned int ms)
{
    usleep(ms * 1000);
}

#endif				/* #ifndef RELAY_WORKER_H */
