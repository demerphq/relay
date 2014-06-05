#include "relay.h"
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
	SAYX(EXIT_FAILURE,"must specity tcp or udp");
    }
    s->proto = proto;
    snprintf(s->to_string,PATH_MAX,
		    "%s@%s:%d",(s->proto == IPPROTO_TCP ? "tcp" : "udp"),
		    inet_ntoa(s->sa.in.sin_addr),ntohs(s->sa.in.sin_port));
    _D("%s",s->to_string);
    free(a);
}

int open_socket(struct sock *s,int flags) {
#define ERROR(fmt,arg...)       \
do {                            \
    if (flags & DO_NOT_EXIT) {  \
        _ENO(fmt,##arg);        \
    } else {                    \
        SAYPX(fmt,##arg);       \
    }                           \
    ok = 0;                     \
} while(0);

    int ok = 1;
    if ((s->socket = socket(s->sa.in.sin_family,s->type,s->proto)) < 0) 
        ERROR("socket[%s]",s->to_string);

    if (flags & DO_BIND) {
        if (bind(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen) )
            ERROR("bind[%s]",s->to_string);
    } 

    if(flags & DO_CONNECT) {
        if (s->proto == IPPROTO_TCP) {
            if (connect(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen) )
                ERROR("connect[%s]",s->to_string);

            if (flags & DO_SET_TIMEOUT) {
                struct timeval tv;
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                if (setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval)) < 0)
                    ERROR("setsockopt[%s]",s->to_string);
            }
        }
    }
    return ok;
#undef ERROR
}
