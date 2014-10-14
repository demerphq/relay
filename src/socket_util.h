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
#define DO_NOT_EXIT     0x04
#define DO_REUSEADDR    0x08
#define DO_EPOLLFD      0x10

#define SOCK_FAKE_FILE  -1
#define SOCK_FAKE_ERROR -2

struct sock {
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
};
typedef struct sock sock_t;

/* util.c */
void socketize(const char *arg, sock_t * s, int default_proto, int conn_dir, char *type_str);
int open_socket(sock_t * s, int flags, int snd, int rcv);
int setnonblocking(int fd);
/* try to get the OS to send our packets more efficiently when sending
 * via TCP. */
static INLINE void cork(struct sock *s, int flag)
{
    if (!s || s->proto != IPPROTO_TCP)
	return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK, (char *) &flag, sizeof(int)) < 0)
	WARN_ERRNO("setsockopt: %s", strerror(errno));
}

#endif				/* #ifndef RELAY_SOCKET_UTIL_H */
