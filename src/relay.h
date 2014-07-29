#ifndef _RELAY_H
#define _RELAY_H 

#include "relay_common.h"
#include "socket_util.h"
#include <sys/epoll.h>
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

#define MAX_EVENTS 32
#define EXPECTED_HEADER_SIZE 4
struct tcp_client {
    char buf[MAX_CHUNK_SIZE + EXPECTED_HEADER_SIZE]; // max chunk size + size of the expected header
    uint32_t pos;
    int fd;
};

#endif
