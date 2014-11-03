#ifndef RELAY_RELAY_H
#define RELAY_RELAY_H

#include <ctype.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "relay_common.h"
#include "socket_util.h"

#define EXPECTED_HEADER_SIZE sizeof(blob_size_t)
#define ASYNC_BUFFER_SIZE (MAX_CHUNK_SIZE + EXPECTED_HEADER_SIZE)
struct tcp_client {
    unsigned char *buf;
    uint32_t pos;
};

#endif				/* #ifndef RELAY_RELAY_H */
