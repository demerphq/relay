#ifndef RELAY_RELAY_H
#define RELAY_RELAY_H

#include "relay_common.h"
#include "socket_util.h"
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

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
    int pos;
};

#endif /* #ifndef RELAY_RELAY_H */
