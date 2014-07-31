#ifndef _RELAY_H
#define _RELAY_H 

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
struct tcp_client {
    union {
        struct packed {
            uint32_t expected;
            char buf[MAX_CHUNK_SIZE];
        }  __attribute__ ((__packed__)) packed;
        char raw[MAX_CHUNK_SIZE + EXPECTED_HEADER_SIZE];
    } frame;
    int pos;
};

#endif
