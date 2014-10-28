#ifndef RELAY_DISK_WRITER_H
#define RELAY_DISK_WRITER_H

#include <pthread.h>

#include "blob.h"
#include "config.h"
#include "relay_common.h"
#include "stats.h"

/* disk worker thread */
struct disk_writer {
    queue_t queue;
    pthread_t tid;

    const config_t *config;

    /* If non-zero, this worker is already exiting. */
    volatile uint32_t exiting;

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
