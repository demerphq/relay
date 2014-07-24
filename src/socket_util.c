#include "socket_util.h"
extern struct config CONFIG;

void socketize(const char *arg, sock_t *s, int default_proto, int conn_dir) {
    char *a = strdup(arg);
    char *p;
    int proto = 0;
    int wrote = 0;
    if ((p = strchr(a, ':')) != NULL) {

        s->sa.in.sin_family = AF_INET;
        /* XXX: error handling? */
        s->sa.in.sin_port = htons(atoi( p + 1 )); /* skip the : */

        /* replace the ":" with a null, effectively strip the proto off the end */
        *p = '\0';

        if ((p = strchr(a, '@')) != NULL) {
            *p++ = '\0'; /* get rid of the @ and move to the next char*/
            if (strcmp("tcp", a) == 0) {
                proto = IPPROTO_TCP;
            } else if (strcmp("udp", a) == 0) {
                proto = IPPROTO_UDP;
            } else {
                DIE_RC(EXIT_FAILURE, "must specify a port");
            }
        } else {
            p= a; /* reset p back to the start of the string */
            proto = default_proto;
        }
        if (proto == IPPROTO_TCP) {
            s->type = SOCK_STREAM;
        } else if (proto == IPPROTO_UDP) {
            s->type = SOCK_DGRAM;
        } else{
            DIE_RC(EXIT_FAILURE, "unknown proto '%d'", proto);
        }

        struct in_addr ip;
        if (inet_aton(p, &ip) == 0) {
            struct hostent * host = gethostbyname2(p, AF_INET);
            if (!host)
                DIE_RC(EXIT_FAILURE, "failed to parse/resolve %s", p);

            memcpy(&(s->sa.in.sin_addr), host->h_addr, host->h_length);
        } else {
            s->sa.in.sin_addr.s_addr = ip.s_addr;
        }

        s->addrlen = sizeof(s->sa.in);
        wrote= snprintf(s->to_string, PATH_MAX,
                    "%s@%s:%d", (s->proto == IPPROTO_TCP ? "tcp" : "udp"),
                    inet_ntoa(s->sa.in.sin_addr), ntohs(s->sa.in.sin_port));
        if (wrote >= PATH_MAX)
            DIE_RC(EXIT_FAILURE, "failed to stringify target descriptor");
        SAY("socket details: %s", s->to_string);
    } else if ( conn_dir == RELAY_CONN_IS_OUTBOUND && ( *a == '/' || *a == '.' ) ) {
        proto = -1;
        wrote= snprintf(s->to_string, PATH_MAX, "%s", a);
        if (wrote >= PATH_MAX)
            DIE_RC(EXIT_FAILURE, "path too long");
        DIE_RC(EXIT_FAILURE, "file not yet implemented");
        SAY("writing to a file: %s", s->to_string);
    } else {
        DIE_RC(EXIT_FAILURE, "must specify a port");
    }
    s->proto = proto;
    free(a);
}

int open_socket(sock_t *s, int flags,int snd, int rcv) {
#define ERROR(fmt, arg...)          \
STMT_START {                        \
    if (flags & DO_NOT_EXIT) {      \
        WARN_ERRNO(fmt, ##arg);     \
    } else {                        \
        DIE(fmt, ##arg);            \
    }                               \
    close(s->socket);               \
    return 0;                       \
} STMT_END

    int ok = 1;
    if ((s->socket = socket(s->sa.in.sin_family, s->type, s->proto)) < 0)
        ERROR("socket[%s]", s->to_string);

    if (flags & DO_BIND) {
        if (bind(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen) )
            ERROR("bind[%s]", s->to_string);
        if (s->proto == IPPROTO_TCP) {
            if (listen(s->socket, SOMAXCONN))
                ERROR("listen[%s]", s->to_string);
        }
    }  else if (flags & DO_CONNECT) {
        if (s->proto == IPPROTO_TCP) {
            if (connect(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen) )
                ERROR("connect[%s]", s->to_string);
            if (CONFIG.tcp_send_timeout > 0) {
                struct timeval timeout;
                timeout.tv_sec = CONFIG.tcp_send_timeout;
                timeout.tv_usec = 0;

                if (setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
                    ERROR("setsockopt[%s]", s->to_string);
            }
        }
    }
    if (snd > 0) {
        if (setsockopt(s->socket, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd)) < 0)
            ERROR("setsockopt[%s]", s->to_string);
    }
    if (rcv > 0) {
        if (setsockopt(s->socket, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv)) < 0)
            ERROR("setsockopt[%s]", s->to_string);
    }
    return ok;
#undef ERROR
}
