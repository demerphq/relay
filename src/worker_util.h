#ifndef RELAY_WORKER_UTIL_H
#define RELAY_WORKER_UTIL_H

#include "socket_util.h"
#include "worker_base.h"

relay_socket_t *open_output_socket_eventually(struct worker_base * base);

#endif                          /* #ifndef RELAY_WORKER_UTIL_H */
