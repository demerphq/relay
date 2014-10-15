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

#if defined(__APPLE__) || defined(__MACH__)
#include <sys/syslimits.h>
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif

#define EXPECTED_HEADER_SIZE 4
#define ASYNC_BUFFER_SIZE (MAX_CHUNK_SIZE + EXPECTED_HEADER_SIZE)
struct tcp_client {
    char *buf;
    uint32_t pos;
};

#endif				/* #ifndef RELAY_RELAY_H */
