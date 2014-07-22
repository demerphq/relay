#ifndef _DISK_WRITER_H
#define _DISK_WRITER_H

#define EXIT_FLAG 1

#include "relay_common.h"
#include <pthread.h>
/* disk worker thread */

struct disk_writer {
    queue_t queue;
    pthread_t tid;

    volatile uint32_t exit;

    stats_basic_counters_t counters;
    stats_basic_counters_t totals;

    char fallback_path[PATH_MAX];
};
typedef struct disk_writer disk_writer_t;

#endif
