#ifndef RELAY_DISK_WRITER_H
#define RELAY_DISK_WRITER_H

#define EXIT_FLAG 1

#include "relay_common.h"
#include "blob.h"
#include "stats.h"
#include <pthread.h>
/* disk worker thread */

struct disk_writer {
    queue_t queue;
    pthread_t tid;

    volatile uint32_t exit;

    stats_basic_counters_t *pcounters;
    stats_basic_counters_t *ptotals;

    char spillway_path[PATH_MAX];
    char last_file_path[PATH_MAX];
    time_t last_epoch;
    int fd;
};
typedef struct disk_writer disk_writer_t;

void *disk_writer_thread(void *arg);

#endif				/* #ifndef RELAY_DISK_WRITER_H */
