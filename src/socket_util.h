#ifndef RELAY_SOCKET_UTIL_H
#define RELAY_SOCKET_UTIL_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "config.h"
#include "relay_common.h"

#define RELAY_CONN_IS_INBOUND   0
#define RELAY_CONN_IS_OUTBOUND  1

#define DO_NOTHING      0x00
#define DO_BIND         0x01
#define DO_CONNECT      0x02
#define DO_REUSEADDR    0x04
#define DO_EPOLLFD      0x08

#define SOCK_FAKE_FILE  -1
#define SOCK_FAKE_ERROR -2

struct relay_socket {
    union sa {
	struct sockaddr_un un;
	struct sockaddr_in in;
    } sa;
    int socket;
    int proto;
    int type;
    char arg[PATH_MAX];
    char to_string[PATH_MAX];
    char arg_clean[PATH_MAX];
    socklen_t addrlen;
    int polling_interval_millisec;
};
typedef struct relay_socket relay_socket_t;

int socketize(const char *arg, relay_socket_t * s, int default_proto, int connection_direction, const char *role);

int open_socket(relay_socket_t * s, int flags, socklen_t snd, socklen_t rcv);

int setnonblocking(int fd);

#endif				/* #ifndef RELAY_SOCKET_UTIL_H */
