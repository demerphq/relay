#ifndef _SOCKET_UTIL_H
#define _SOCKET_UTIL_H

#include "relay_common.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>


#define RELAY_CONN_IS_INBOUND   0
#define RELAY_CONN_IS_OUTBOUND  1

#define DO_NOTHING      0x00
#define DO_BIND         0x01
#define DO_CONNECT      0x02
#define DO_NOT_EXIT     0x04
#define DO_REUSEADDR    0x08

struct sock {
    union sa {
        struct sockaddr_un un;
        struct sockaddr_in in;
    } sa;
    int socket;
    int proto;
    int type;
    char to_string[PATH_MAX];
    socklen_t addrlen;
};
typedef struct sock sock_t;

/* util.c */
void socketize(const char *arg, sock_t *s, int default_proto, int conn_dir );
int open_socket(sock_t *s, int flags, int snd, int rcv);

/* try to get the OS to send our packets more efficiently when sending
 * via TCP. */
static INLINE void cork(struct sock *s,int flag) {
    if (!s || s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        WARN_ERRNO("setsockopt: %s", strerror(errno));
}


#endif
