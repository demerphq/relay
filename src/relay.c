#include "relay.h"

#include "config.h"
#include "control.h"
#include "setproctitle.h"
#include "timer.h"
#include "worker.h"
#include "worker_pool.h"

#define MAX_BUF_LEN 128

static void sig_handler(int signum);
static void stop_listener(pthread_t server_tid);
static void final_shutdown(pthread_t server_tid);
sock_t *s_listen;
extern config_t CONFIG;
graphite_worker_t *graphite_worker;

stats_basic_counters_t RECEIVED_STATS = {
    .received_count = 0,	/* number of items we have received */
    .sent_count = 0,		/* number of items we have sent */
    .partial_count = 0,		/* number of items we have spilled */
    .spilled_count = 0,		/* number of items we have spilled */
    .error_count = 0,		/* number of items that had an error */
    .disk_count = 0,		/* number of items we have written to disk */

    .send_elapsed_usec = 0,	/* elapsed time in microseconds that we spent sending data */
    .active_connections = 0,	/* current number of active inbound tcp connections */
};

static void spawn(pthread_t * tid, void *(*func) (void *), void *arg, int type)
{
    pthread_t unused;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, type);
    pthread_create(tid ? tid : &unused, &attr, func, arg);
    pthread_attr_destroy(&attr);
}

static inline blob_t *buf_to_blob_enqueue(char *buf, size_t size)
{
    blob_t *b;
    if (size == 0) {
	if (0)
	    WARN("Received 0 byte packet, not forwarding.");
	return NULL;
    }

    RELAY_ATOMIC_INCREMENT(RECEIVED_STATS.received_count, 1);
    b = blob_new(size);
    memcpy(BLOB_BUF_addr(b), buf, size);
    enqueue_blob_for_transmission(b);
    return b;
}

void *udp_server(void *arg)
{
    sock_t *s = (sock_t *) arg;
#ifdef PACKETS_PER_SECOND
    uint32_t packets = 0, prev_packets = 0;
    uint32_t epoch, prev_epoch = 0;
#endif
    char buf[MAX_CHUNK_SIZE];	/* unused, but makes recv() happy */
    while (not_stopped()) {
	ssize_t received = recv(s->socket, buf, MAX_CHUNK_SIZE, 0);
#ifdef PACKETS_PER_SECOND
	if ((epoch = time(0)) != prev_epoch) {
	    SAY("packets: %d", packets - prev_packets);
	    prev_epoch = epoch;
	    prev_packets = packets;
	}
	packets++;
#endif
	if (received < 0) {
	    WARN_ERRNO("recv failed");
	    break;
	}
	buf_to_blob_enqueue(buf, received);
    }
    set_stopped();
    pthread_exit(NULL);
}

#define EXPECTED_PACKET_SIZE(x) (*(__blob_size_t *) &(x)->buf[0])

/* The server socket and the client contexts. */
typedef struct {
    /* The number of clients. */
    volatile nfds_t nfds;

    /* The file descriptors.  The pfds[0] is the server socket,
     * the pfds[1...] are the client sockets. */
    struct pollfd *pfds;

    /* The clients[0] is unused (it is the server),
     * the pfds[1..] are the client contexts. */
    struct tcp_client *clients;
} tcp_server_context_t;

#define TCP_FAILURE 0
#define TCP_SUCCESS 1

static void tcp_context_init(tcp_server_context_t * ctxt)
{
    ctxt->nfds = 0;

    /* Just the server socket. */
    ctxt->pfds = calloc_or_die(sizeof(struct pollfd));

    /* tcp_add_fd() will set this right soon. */
    ctxt->pfds[0].fd = -1;

    /* The clients[0] is unused but let's make it a "null client" so
     * that looping over the contexts can have less special cases. */
    ctxt->clients = calloc_or_die(sizeof(struct tcp_client));
    ctxt->clients[0].buf = NULL;
    ctxt->clients[0].pos = 0;
}

static void tcp_add_fd(tcp_server_context_t * ctxt, int fd)
{
    setnonblocking(fd);
    ctxt->pfds[ctxt->nfds].fd = fd;
    ctxt->pfds[ctxt->nfds].events = POLLIN;
    ctxt->nfds++;
}

static void tcp_context_realloc(tcp_server_context_t * ctxt, nfds_t n)
{
    ctxt->pfds = realloc_or_die(ctxt->pfds, n * sizeof(struct pollfd));
    ctxt->clients = realloc_or_die(ctxt->clients, n * sizeof(struct tcp_client));
}

/* Returns TCP_FAILURE if failed, TCP_SUCCESS if successful.
 * If not successful the server should probably exit. */
static int tcp_accept(tcp_server_context_t * ctxt, int server_fd)
{
    int fd = accept(server_fd, NULL, NULL);
    if (fd == -1) {
	WARN_ERRNO("accept");
	return TCP_FAILURE;
    }
    RELAY_ATOMIC_INCREMENT(RECEIVED_STATS.active_connections, 1);

    tcp_context_realloc(ctxt, ctxt->nfds + 1);

    ctxt->clients[ctxt->nfds].pos = 0;
    ctxt->clients[ctxt->nfds].buf = calloc_or_die(ASYNC_BUFFER_SIZE);
    ctxt->pfds[ctxt->nfds].revents = 0;

    tcp_add_fd(ctxt, fd);

    /* WARN("CREATE %p fd: %d", ctxt->clients[ctxt->nfds].buf, fd); */

    return TCP_SUCCESS;
}

/* Returns TCP_FAILURE if failed, TCP_SUCCESS if successful.
 * If successful, we should move on to the next connection.
 * (Note that the success may be a full or a partial packet.)
 * If not successful, this connection should probably be removed. */
static int tcp_read(tcp_server_context_t * ctxt, nfds_t i)
{
    assert(i < ctxt->nfds);
    struct tcp_client *client = &ctxt->clients[i];

    /* try to read as much as possible */
    ssize_t try_to_read = ASYNC_BUFFER_SIZE - (int) client->pos;

    if (try_to_read <= 0) {
	WARN("try_to_read: %zd, pos: %u", try_to_read, client->pos);
	return TCP_FAILURE;
    }

    ssize_t received = recv(ctxt->pfds[i].fd, client->buf + client->pos, try_to_read, 0);
    if (received <= 0) {
	if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
	    return TCP_SUCCESS;

	return TCP_FAILURE;
    }

    client->pos += received;

    /* NOTE: the flow control of this loop is somewhat unusual. */
    for (;;) {

	if (client->pos < EXPECTED_HEADER_SIZE)
	    return TCP_SUCCESS;

	if (EXPECTED_PACKET_SIZE(client) > MAX_CHUNK_SIZE) {
	    WARN("received frame (%d) > MAX_CHUNK_SIZE(%d)", EXPECTED_PACKET_SIZE(client), MAX_CHUNK_SIZE);
	    return TCP_FAILURE;
	}

	if (client->pos >= EXPECTED_PACKET_SIZE(client) + EXPECTED_HEADER_SIZE) {
	    /* If this packet came from a TCP connection, its first four
	     * bytes are supposed to be the length, so let's skip them. */
	    buf_to_blob_enqueue(client->buf + EXPECTED_HEADER_SIZE, EXPECTED_PACKET_SIZE(client));

	    client->pos -= EXPECTED_PACKET_SIZE(client) + EXPECTED_HEADER_SIZE;
	    if (client->pos > 0) {
		/* [ h ] [ h ] [ h ] [ h ] [ D ] [ D ] [ D ] [ h ] [ h ] [ h ] [ h ] [ D ]
		 *                                                                     ^ pos(12)
		 * after we remove the first packet + header it becomes:
		 * [ h ] [ h ] [ h ] [ h ] [ D ] [ D ] [ D ] [ h ] [ h ] [ h ] [ h ] [ D ]
		 *                           ^ pos (5)
		 * and then we copy from header + data, to position 0, 5 bytes
		 *
		 * [ h ] [ h ] [ h ] [ h ] [ D ]
		 *                           ^ pos (5) */

		memmove(client->buf, client->buf + EXPECTED_HEADER_SIZE + EXPECTED_PACKET_SIZE(client), client->pos);
		if (client->pos >= EXPECTED_HEADER_SIZE)
		    continue;	/* there is one more packet left in the buffer, consume it */
	    }
	}

	return TCP_SUCCESS;
    }
}

/* Close the given client connection. */
static void tcp_client_close(tcp_server_context_t * ctxt, nfds_t i)
{
    /* We could pass in both client and i, but then there's danger of mismatch. */
    assert(i < ctxt->nfds);
    struct tcp_client *client = &ctxt->clients[i];

    /* WARN("[%d] DESTROY %p %d %d fd: %d vs %d", i, client->buf, client->x, i, ctxt->pfds[i].fd, client->fd); */

    /* In addition to releasing resources (free, close) also reset
     * the various fields to invalid values (NULL, -1) just in case
     * someone accidentally tries using them. */
    shutdown(ctxt->pfds[i].fd, SHUT_RDWR);
    close(ctxt->pfds[i].fd);
    ctxt->pfds[i].fd = -1;
    free(client->buf);
    client->buf = NULL;
}

/* Remove the client connection (first closes it) */
static void tcp_client_remove(tcp_server_context_t * ctxt, nfds_t i)
{
    assert(i < ctxt->nfds);
    tcp_client_close(ctxt, i);

    /* Remove the connection by shifting left
     * the connections coming after it. */
    {
	nfds_t tail = ctxt->nfds - i - 1;
	assert(tail < ctxt->nfds);
	memcpy(ctxt->pfds + i, ctxt->pfds + i + 1, tail * sizeof(struct pollfd));
	memcpy(ctxt->clients + i, ctxt->clients + i + 1, tail * sizeof(struct tcp_client));
    }

    ctxt->nfds--;
    tcp_context_realloc(ctxt, ctxt->nfds);
    RELAY_ATOMIC_DECREMENT(RECEIVED_STATS.active_connections, 1);
}

static void tcp_context_close(tcp_server_context_t * ctxt)
{
    for (nfds_t i = 0; i < ctxt->nfds; i++) {
	tcp_client_close(ctxt, i);
    }
    /* Release and reset. */
    free(ctxt->pfds);
    free(ctxt->clients);
    ctxt->nfds = 0;		/* Cannot be -1 since nfds_t is unsigned. */
    ctxt->pfds = NULL;
    ctxt->clients = NULL;
}

void *tcp_server(void *arg)
{
    sock_t *s = (sock_t *) arg;
    tcp_server_context_t ctxt;

    tcp_context_init(&ctxt);

    tcp_add_fd(&ctxt, s->socket);

    RELAY_ATOMIC_AND(RECEIVED_STATS.active_connections, 0);

    for (;;) {
	int rc = poll(ctxt.pfds, ctxt.nfds, CONFIG.polling_interval_ms);
	if (rc == -1) {
	    if (errno == EINTR)
		continue;
	    WARN_ERRNO("poll");
	    goto out;
	} else {
	    for (nfds_t i = 0; i < ctxt.nfds; i++) {
		if (!ctxt.pfds[i].revents)
		    continue;
		if (ctxt.pfds[i].fd == s->socket) {
		    if (!tcp_accept(&ctxt, s->socket))
			goto out;
		} else {
		    if (!tcp_read(&ctxt, i))
			tcp_client_remove(&ctxt, i);
		}
	    }
	}
    }

  out:
    tcp_context_close(&ctxt);
    set_stopped();
    pthread_exit(NULL);
}

pthread_t setup_listener(config_t * config)
{
    pthread_t server_tid = 0;

    socketize(config->argv[0], s_listen, IPPROTO_UDP, RELAY_CONN_IS_INBOUND, "listener");

    /* must open the socket BEFORE we create the worker pool */
    open_socket(s_listen, DO_BIND | DO_REUSEADDR | DO_EPOLLFD, 0, config->server_socket_rcvbuf);

    /* create worker pool /after/ we open the socket, otherwise we
     * might leak worker threads. */

    if (s_listen->proto == IPPROTO_UDP)
	spawn(&server_tid, udp_server, s_listen, PTHREAD_CREATE_JOINABLE);
    else
	spawn(&server_tid, tcp_server, s_listen, PTHREAD_CREATE_JOINABLE);

    return server_tid;
}

static int serve(config_t * config)
{
    pthread_t server_tid = 0;

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, sig_handler);
    signal(SIGHUP, sig_handler);

    setproctitle("starting");

    s_listen = calloc_or_die(sizeof(*s_listen));

    worker_pool_init_static(config);
    server_tid = setup_listener(config);
    graphite_worker = calloc_or_die(sizeof(graphite_worker_t));
    pthread_create(&graphite_worker->tid, NULL, graphite_worker_thread, graphite_worker);

    for (;;) {
	uint32_t control;

	control = get_control_val();
	if (control & RELAY_STOP) {
	    break;
	} else if (control & RELAY_RELOAD) {
	    if (config_reload(config)) {
		stop_listener(server_tid);
		server_tid = setup_listener(config);
		worker_pool_reload_static(config);
		/* XXX: check me */
		/* check and see if we need to stop the old graphite processor and replace it */
		graphite_worker_destroy(graphite_worker);
		pthread_create(&graphite_worker->tid, NULL, graphite_worker_thread, graphite_worker);
	    }
	    unset_control_bits(RELAY_RELOAD);
	}

	update_process_status(RELAY_ATOMIC_READ(RECEIVED_STATS.received_count),
			      RELAY_ATOMIC_READ(RECEIVED_STATS.active_connections));
	sleep(1);
    }
    final_shutdown(server_tid);
    SAY("bye");
    closelog();
    return (0);
}

static void sig_handler(int signum)
{
    switch (signum) {
    case SIGHUP:
	set_control_bits(RELAY_RELOAD);
	break;
    case SIGTERM:
    case SIGINT:
	set_stopped();
	break;
    default:
	WARN("IGNORE: unexpected signal %d", signum);
    }
}

static void stop_listener(pthread_t server_tid)
{
    shutdown(s_listen->socket, SHUT_RDWR);
    /* TODO: if the relay is interrupted rudely (^C), final_shutdown()
     * is called, which will call stop_listener(), and this close()
     * triggers the ire of the clang threadsanitizer, since the socket
     * was opened by a worker thread with a recv() in udp_server, but
     * the shutdown happens in the main thread. */
    close(s_listen->socket);
    pthread_join(server_tid, NULL);
}

static void final_shutdown(pthread_t server_tid)
{
    graphite_worker_destroy(graphite_worker);
    stop_listener(server_tid);
    worker_pool_destroy_static();
    free(s_listen);
    sleep(1);			/* give a chance to the detachable tcp worker threads to pthread_exit() */
    config_destroy();
    destroy_proctitle();
}

int main(int argc, char **argv)
{
    config_init(argc, argv);
    initproctitle(argc, argv);
    return serve(&CONFIG);
}
