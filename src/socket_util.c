#include "socket_util.h"

#include <ctype.h>
#include <libgen.h>

#include "global.h"
#include "log.h"
#include "string_util.h"

#define DEBUG_SOCKETIZE 0

static int socketize_validate(const char *arg, char *a, relay_socket_t * s, int default_proto, int connection_direction)
{
    char *p;
    int proto = SOCK_FAKE_ERROR;
    int wrote = 0;

    if ((p = strchr(a, ':')) != NULL) {

	s->sa.in.sin_family = AF_INET;

	if (!isdigit(p[1]) || p[1] == '0') {
	    WARN("Invalid port number '%s' in '%s'", p + 1, arg);
	    return 0;
	}
	char *endp;
	long port = strtol(p + 1, &endp, 10);
	if (port < 0 || port > 65535 || *endp) {
	    WARN("Invalid port number '%s' in '%s'", p + 1, arg);
	    return 0;
	}
	s->sa.in.sin_port = htons(port);

	/* replace the ":" with a null, effectively strip the proto off the end */
	*p = '\0';

	if ((p = strchr(a, '@')) != NULL) {
	    if (DEBUG_SOCKETIZE)
		SAY("found '@'");
	    *p++ = '\0';	/* get rid of the @ and move to the next char */
	    if (STREQ("tcp", a)) {
		if (DEBUG_SOCKETIZE)
		    SAY("protocol is tcp");
		proto = IPPROTO_TCP;
	    } else if (STREQ("udp", a)) {
		if (DEBUG_SOCKETIZE)
		    SAY("protocol is udp");
		proto = IPPROTO_UDP;
	    } else {
		WARN("Unknown protocol '%s' in argument '%s'", a, arg);
		return 0;
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
	    WARN("Unknown proto '%d'", proto);
	    return 0;
	}

	struct in_addr ip;
	if (inet_aton(p, &ip) == 0) {
	    struct hostent *host = gethostbyname2(p, AF_INET);
	    if (!host) {
		WARN("Failed to parse/resolve hostname %s", p);
		return 0;
	    }

	    memcpy(&(s->sa.in.sin_addr), host->h_addr, host->h_length);
	} else {
	    s->sa.in.sin_addr.s_addr = ip.s_addr;
	}

	s->addrlen = sizeof(s->sa.in);
	wrote =
	    snprintf(s->to_string, PATH_MAX, "%s@%s:%d",
		     (proto == IPPROTO_TCP ? "tcp" : "udp"), inet_ntoa(s->sa.in.sin_addr), ntohs(s->sa.in.sin_port));
	if (wrote < 0 || wrote >= PATH_MAX) {
	    WARN("Failed to stringify target descriptor");
	    return 0;
	}
	if (DEBUG_SOCKETIZE)
	    SAY("socket details: %s", s->to_string);
    } else if (connection_direction == RELAY_CONN_IS_OUTBOUND && (*a == '/' || *a == '.')) {
	proto = SOCK_FAKE_FILE;
	wrote = snprintf(s->to_string, PATH_MAX, "file@%s", a);
	if (wrote < 0 || wrote >= PATH_MAX) {
	    SAY("Path too long");
	    return 0;
	}
	if (DEBUG_SOCKETIZE)
	    SAY("Writing to a file: %s", s->to_string);

	struct stat st;
	char *dir = dirname(a);	/* NOTE: MODIFIES a! */
	if (*dir && !(stat(dir, &st) == 0 && S_ISDIR(st.st_mode))) {
	    WARN("%s: not a directory (for file %s)", dir, arg);
	    return 0;
	}
    } else {
	WARN("Must specify a port in '%s'", arg);
	return 0;
    }
    if (!(proto == IPPROTO_TCP || proto == IPPROTO_UDP || proto == SOCK_FAKE_FILE)) {
	WARN("Unexpcted proto %d", proto);
	return 0;
    }

    s->proto = proto;

    return 1;
}

int socketize(const char *arg, relay_socket_t * s, int default_proto, int connection_direction, const char *role)
{
    char *a = strdup(arg);

    SAY("Socketizing '%s' for %s", arg, role);

    strncpy(s->arg, arg, PATH_MAX);
    strncpy(s->arg_clean, arg, PATH_MAX);
    scrub_nonalnum(s->arg_clean, PATH_MAX);

    int valid = socketize_validate(arg, a, s, default_proto, connection_direction);

    free(a);

    return valid;
}

#define FATAL_CLOSE_FAIL(s, fmt, arg...)	\
STMT_START {                        \
    FATAL_ERRNO(fmt, ##arg);	    \
    close(s->socket);               \
    return 0;                       \
} STMT_END

int open_socket(relay_socket_t * s, int flags, int snd, int rcv)
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
	FATAL_CLOSE_FAIL(s, "socket[%s]", s->to_string);

    if (flags & DO_BIND) {
	if (flags & DO_REUSEADDR) {
	    int optval = 1;
	    if (setsockopt(s->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
		FATAL_CLOSE_FAIL(s, "setsockopt[REUSEADDR]");
	}
	if (bind(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen))
	    FATAL_CLOSE_FAIL(s, "bind[%s]", s->to_string);
	if (s->proto == IPPROTO_TCP) {
	    if (listen(s->socket, SOMAXCONN))
		FATAL_CLOSE_FAIL(s, "listen[%s]", s->to_string);
	}
    } else if (flags & DO_CONNECT) {
	if (s->proto == IPPROTO_TCP) {
	    if (connect(s->socket, (struct sockaddr *) &s->sa.in, s->addrlen))
		FATAL_CLOSE_FAIL(s, "connect[%s]", s->to_string);
	    if (GLOBAL.config->tcp_send_timeout_millisec > 0) {
		struct timeval timeout;
		timeout.tv_sec = GLOBAL.config->tcp_send_timeout_millisec / 1000;
		timeout.tv_usec = 1000 * (GLOBAL.config->tcp_send_timeout_millisec % 1000);

		if (setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof(timeout)) < 0)
		    FATAL_CLOSE_FAIL(s, "setsockopt[%s]", s->to_string);
	    }
	}
    }
    if (snd > 0) {
	if (setsockopt(s->socket, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd))
	    < 0)
	    FATAL_CLOSE_FAIL(s, "setsockopt[%s]", s->to_string);
    }
    if (rcv > 0) {
	if (setsockopt(s->socket, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv))
	    < 0)
	    FATAL_CLOSE_FAIL(s, "setsockopt[%s]", s->to_string);
    }
    if (ok)
	SAY("Connected %s", s->to_string);
    else
	FATAL_CLOSE_FAIL(s, "Failed to connect %s", s->to_string);

    return ok;
}

relay_socket_t *open_socket_eventually(relay_socket_t * s, const config_t * config)
{
    relay_socket_t *sck = NULL;
    int nap = config->sleep_after_disaster_millisec;

    while (!sck) {
	/* lets try to open one */
	if (open_socket(s, DO_CONNECT, config->server_socket_sndbuf_bytes, 0)) {
	    sck = s;
	} else {
	    /* no socket - wait a while, double the wait, and then redo the loop */
	    SAY("waiting %d millisec to retry socket %s", nap, s->to_string);
	    worker_wait_millisec(nap);
	    nap = 2 * nap + 1;	/* XXX maybe also randomize a bit? */
	    /* XXX maximum wait after which to give up? */
	}
    }

    return sck;
}

int setnonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
	return flags;
    flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}
