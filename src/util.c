#include "relay.h"
static char OUTPUT[1024];
void socketize(const char *arg,struct sock *s) {
    char *a = strdup(arg);
    char *p;
    int proto = 0;
    s->type = SOCK_DGRAM;
    if ((p = strchr(a,':')) != NULL) {
        
        s->sa.in.sin_family = AF_INET;
        s->sa.in.sin_port = htons(atoi( p + 1 )); /* skip the : */
        *p = '\0';

        if ((p = strchr(a,'@')) != NULL) {
            *p++ = '\0'; /* get rid of the @ */
            if (strcmp("tcp",a) == 0) { 
                proto = IPPROTO_TCP;
                s->type = SOCK_STREAM;
            } else
                proto = IPPROTO_UDP;
        } else {
            p = a;
        }
        
        struct in_addr ip;
        if (inet_aton(p,&ip) == 0) {
            struct hostent * host = gethostbyname2(p,AF_INET);
            if (!host) 
                SAYX(EXIT_FAILURE,"failed to parse/resolve %s",p); 

            memcpy(&(s->sa.in.sin_addr),host->h_addr,host->h_length);
        } else {
            s->sa.in.sin_addr.s_addr = ip.s_addr;
        }

        s->addrlen = sizeof(s->sa.in);
    } else {
        s->sa.un.sun_family = AF_UNIX; 
        strncpy(s->sa.un.sun_path,a,sizeof(s->sa.un.sun_path) - 1);
        s->addrlen = sizeof(s->sa.un);
    }
    s->proto = proto;
    _D("%s",socket_to_string(s));
    free(a);
}
char *socket_to_string(struct sock *s) {
    char *type = s->type == SOCK_DGRAM ? "SOCK_DGRAM" : "SOCK_STREAM";
    if (s->sa.in.sin_family == AF_INET)
        snprintf(OUTPUT,sizeof(OUTPUT),
                        "%s@%s:%d(%s)",(s->proto == IPPROTO_TCP ? "tcp" : "udp"),
                                   inet_ntoa(s->sa.in.sin_addr),ntohs(s->sa.in.sin_port),type);
    else 
        snprintf(OUTPUT,sizeof(OUTPUT),"UNIX@%s(%s)",s->sa.un.sun_path,type);

    return OUTPUT;
}

 void open_socket(struct sock *s,int do_bind) {
    if ((s->socket = socket(s->sa.in.sin_family,s->type,s->proto)) < 0)
        SAYPX("socket[%s]",socket_to_string(s))
    
    if (do_bind == DO_BIND) {
        if (bind(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen) )
            SAYPX("bind[%s]",socket_to_string(s));
    } else if(do_bind == DO_CONNECT) {
        if (s->proto == IPPROTO_TCP) {
            if (connect(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen) )
                SAYPX("connect[%s]",socket_to_string(s));

            struct timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            if (setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval)) < 0)
                SAYPX("setsockopt[%s]",socket_to_string(s));
        }
    }
}
