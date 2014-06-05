#ifndef _UTIL_H
#define _UTIL_H

#include "relay_common.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

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

/* util.c */
void socketize(const char *arg,struct sock *s);
int open_socket(struct sock *s,int do_bind);

#endif
