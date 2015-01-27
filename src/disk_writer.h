#ifndef RELAY_DISK_WRITER_H
#define RELAY_DISK_WRITER_H

#include <pthread.h>

#include "blob.h"
#include "relay_common.h"
#include "stats.h"
#include "worker_base.h"

/* disk worker thread */
struct disk_writer {
    struct worker_base base;

    queue_t queue;

    /* These are pointing back to the socket worker's counters. */
    stats_basic_counters_t *counters;
    stats_basic_counters_t *recents;
    stats_basic_counters_t *totals;

    char spill_path[PATH_MAX];
    char last_file_path[PATH_MAX];

    int spill_path_created;

    time_t last_epoch;

    int fd;
};
typedef struct disk_writer disk_writer_t;

void *disk_writer_thread(void *arg);

#endif                          /* #ifndef RELAY_DISK_WRITER_H */
