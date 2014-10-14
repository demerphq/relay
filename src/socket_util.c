#include "socket_util.h"

#include "string_util.h"

extern config_t CONFIG;

#define DEBUG_SOCKETIZE 0

void socketize(const char *arg, sock_t * s, int default_proto, int conn_dir, char *type_str)
{
    char *a = strdup(arg);
    char *p;
    int proto = SOCK_FAKE_ERROR;
    int wrote = 0;

    strncpy(s->arg, arg, PATH_MAX);
    strncpy(s->arg_clean, arg, PATH_MAX);
    scrub_nonalnum(s->arg_clean, PATH_MAX);

    SAY("socketizing %s argument '%s'", type_str, arg);
    if ((p = strchr(a, ':')) != NULL) {

	s->sa.in.sin_family = AF_INET;
	/* XXX: error handling? */
	s->sa.in.sin_port = htons(atoi(p + 1));	/* skip the : */

	/* replace the ":" with a null, effectively strip the proto off the end */
	*p = '\0';

	if ((p = strchr(a, '@')) != NULL) {
	    if (DEBUG_SOCKETIZE)
		SAY("found '@'");
	    *p++ = '\0';	/* get rid of the @ and move to the next char */
	    if (strcmp("tcp", a) == 0) {
		if (DEBUG_SOCKETIZE)
		    SAY("protocol is tcp");
		proto = IPPROTO_TCP;
	    } else if (strcmp("udp", a) == 0) {
		if (DEBUG_SOCKETIZE)
		    SAY("protocol is udp");
		proto = IPPROTO_UDP;
	    } else {
		DIE_RC(EXIT_FAILURE, "unknown protocol '%s' in argument '%s'", a, arg);
	    }
	} else {
	    if (DEBUG_SOCKETIZE)
		SAY("did not find '@'");
	    p = a;		/* reset p back to the start of the string */
	    proto = default_proto;
	}
	if (proto == IPPROTO_TCP) {
	    s->type = SOCK_STREAM;
	} else if (proto == IPPROTO_UDP) {
	    s->type = SOCK_DGRAM;
	} else {
	    DIE_RC(EXIT_FAILURE, "unknown proto '%d'", proto);
	}

	struct in_addr ip;
	if (inet_aton(p, &ip) == 0) {
	    struct hostent *host = gethostbyname2(p, AF_INET);
	    if (!host)
		DIE_RC(EXIT_FAILURE, "failed to parse/resolve %s", p);

	    memcpy(&(s->sa.in.sin_addr), host->h_addr, host->h_length);
	} else {
	    s->sa.in.sin_addr.s_addr = ip.s_addr;
	}

	s->addrlen = sizeof(s->sa.in);
	wrote =
	    snprintf(s->to_string, PATH_MAX, "%s@%s:%d",
		     (proto == IPPROTO_TCP ? "tcp" : "udp"), inet_ntoa(s->sa.in.sin_addr), ntohs(s->sa.in.sin_port));
	if (wrote >= PATH_MAX)
	    DIE_RC(EXIT_FAILURE, "failed to stringify target descriptor");
	if (DEBUG_SOCKETIZE)
	    SAY("socket details: %s", s->to_string);
    } else if (conn_dir == RELAY_CONN_IS_OUTBOUND && (*a == '/' || *a == '.')) {
	proto = SOCK_FAKE_FILE;
	assert(proto != IPPROTO_TCP && proto != IPPROTO_UDP);
	wrote = snprintf(s->to_string, PATH_MAX, "file@%s", a);
	if (wrote >= PATH_MAX)
	    DIE_RC(EXIT_FAILURE, "path too long");
	if (DEBUG_SOCKETIZE)
	    SAY("writing to a file: %s", s->to_string);
    } else {
	DIE_RC(EXIT_FAILURE, "must specify a port in '%s'", arg);
    }
    assert(proto == IPPROTO_TCP || proto == IPPROTO_UDP || proto == SOCK_FAKE_FILE);
    s->proto = proto;
    free(a);
}

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

int open_socket(sock_t * s, int flags, int snd, int rcv)
{
    int ok = 1;

    if (s->proto == SOCK_FAKE_FILE) {
	/* its actually a file! */
	s->socket = open(s->arg, O_WRONLY | O_APPEND | O_CREAT, 0640);
	if (s->socket < 0) {
	    WARN_ERRNO("failed to open file '%s' (as a fake socket)", s->to_string);
	    return 0;
	} else {
	    return 1;
	}
    }

    if ((s->socket = socket(s->sa.in.sin_family, s->type, s->proto)) < 0)
	ERROR("socket[%s]", s->to_string);

    if (flags & DO_BIND) {
	if (flags & DO_REUSEADDR) {
	    int optval = 1;
	    if (setsockopt(s->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
		ERROR("setsockopt[REUSEADDR]");
	}
	if (bind(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen))
	    ERROR("bind[%s]", s->to_string);
	if (s->proto == IPPROTO_TCP) {
	    if (listen(s->socket, SOMAXCONN))
		ERROR("listen[%s]", s->to_string);
	}
    } else if (flags & DO_CONNECT) {
	if (s->proto == IPPROTO_TCP) {
	    if (connect(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen))
		ERROR("connect[%s]", s->to_string);
	    if (CONFIG.tcp_send_timeout > 0) {
		struct timeval timeout;
		timeout.tv_sec = CONFIG.tcp_send_timeout;
		timeout.tv_usec = 0;

		if (setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof(timeout)) < 0)
		    ERROR("setsockopt[%s]", s->to_string);
	    }
	}
    }
    if (snd > 0) {
	if (setsockopt(s->socket, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd))
	    < 0)
	    ERROR("setsockopt[%s]", s->to_string);
    }
    if (rcv > 0) {
	if (setsockopt(s->socket, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv))
	    < 0)
	    ERROR("setsockopt[%s]", s->to_string);
    }
    return ok;
#undef ERROR
}

int setnonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
	return flags;
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}
