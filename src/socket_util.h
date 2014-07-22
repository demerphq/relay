#ifndef _SOCKET_UTIL_H
#define _SOCKET_UTIL_H

#include "relay_common.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

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
void socketize(const char *arg, sock_t *s);
int open_socket(sock_t *s, int flags, int snd, int rcv);

#ifdef TCP_CORK
/* try to get the OS to send our packets more efficiently when sending
 * via TCP. */
static INLINE void cork(struct sock *s,int flag) {
    if (!s || s->proto != IPPROTO_TCP)
        return;
    if (setsockopt(s->socket, IPPROTO_TCP, TCP_CORK , (char *) &flag, sizeof(int)) < 0)
        WARN_ERRNO("setsockopt: %s", strerror(errno));
}
#else
#define cork(a,b)
#endif

#endif
