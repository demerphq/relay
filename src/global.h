#ifndef RELAY_GLOBAL_H
#define RELAY_GLOBAL_H

#include "config.h"
#include "graphite_worker.h"
#include "socket_worker_pool.h"

struct relay_global {
    volatile uint32_t control;
    volatile int exit_code;
    config_t *config;
    relay_socket_t *listener;
    graphite_worker_t *graphite_worker;
    socket_worker_pool_t pool;

    int64_t blob_active_count;
    int64_t blob_active_bytes;
    int64_t blob_active_refcnt_bytes;
    int64_t blob_total_count;
    int64_t blob_total_bytes;
    int64_t blob_total_refcnt_bytes;
    int64_t blob_total_sizes[32];       /* ceil(log2(size)) buckets */
    int64_t blob_total_ored_buckets;    /* OR of all the seen bucket indices */
};
typedef struct relay_global relay_global_t;

extern relay_global_t GLOBAL;

#endif                          /* #ifndef RELAY_GLOBAL_H */
