#include "relay.h"

#include "config.h"
#include "control.h"
#include "setproctitle.h"
#include "string_util.h"
#include "timer.h"
#include "worker.h"
#include "worker_pool.h"

#define PROCESS_STATUS_BUF_LEN 256

static void sig_handler(int signum);
static void stop_listener(pthread_t server_tid);
static void final_shutdown(pthread_t server_tid);

relay_socket_t *listener;
graphite_worker_t *graphite_worker;

extern config_t CONFIG;

stats_basic_counters_t RECEIVED_STATS = {
    .received_count = 0,	/* number of items we have received */
    .sent_count = 0,		/* number of items we have sent */
    .partial_count = 0,		/* number of items we have spilled */
    .spilled_count = 0,		/* number of items we have spilled */
    .error_count = 0,		/* number of items that had an error */
    .disk_count = 0,		/* number of items we have written to disk */

    .send_elapsed_usec = 0,	/* elapsed time in microseconds that we spent sending data */
    .tcp_connections = 0,	/* current number of active inbound tcp connections */
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

static inline blob_t *buf_to_blob_enqueue(unsigned char *buf, size_t size)
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
    relay_socket_t *s = (relay_socket_t *) arg;
#ifdef PACKETS_PER_SECOND
    uint32_t packets = 0, prev_packets = 0;
    uint32_t epoch, prev_epoch = 0;
#endif
    unsigned char buf[MAX_CHUNK_SIZE];
    while (control_is_not(RELAY_STOPPING)) {
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
    pthread_exit(NULL);
}

/* The wire format is little-endian. */
#define EXPECTED_PACKET_SIZE(x) ((x)->buf[0] | (x)->buf[1] << 8 | (x)->buf[2] << 16 | (x)->buf[3] << 24)

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
    RELAY_ATOMIC_INCREMENT(RECEIVED_STATS.tcp_connections, 1);

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
	/* Partial header: better to declare success and retry later. */
	if (client->pos < EXPECTED_HEADER_SIZE)
	    return TCP_SUCCESS;

	blob_size_t expected_packet_size = EXPECTED_PACKET_SIZE(client);

	if (expected_packet_size > MAX_CHUNK_SIZE) {
	    WARN("received frame (%d) > MAX_CHUNK_SIZE (%d)", expected_packet_size, MAX_CHUNK_SIZE);
	    return TCP_FAILURE;
	}

	if (client->pos >= expected_packet_size + EXPECTED_HEADER_SIZE) {
	    /* Since this packet came from a TCP connection, its first four
	     * bytes are supposed to be the length, so let's skip them. */
	    buf_to_blob_enqueue(client->buf + EXPECTED_HEADER_SIZE, expected_packet_size);

	    client->pos -= expected_packet_size + EXPECTED_HEADER_SIZE;
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

		memmove(client->buf, client->buf + EXPECTED_HEADER_SIZE + expected_packet_size, client->pos);
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
    RELAY_ATOMIC_DECREMENT(RECEIVED_STATS.tcp_connections, 1);
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
    relay_socket_t *s = (relay_socket_t *) arg;
    tcp_server_context_t ctxt;

    tcp_context_init(&ctxt);

    tcp_add_fd(&ctxt, s->socket);

    RELAY_ATOMIC_AND(RECEIVED_STATS.tcp_connections, 0);

    for (;;) {
	int rc = poll(ctxt.pfds, ctxt.nfds, s->polling_interval_millisec);
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
    pthread_exit(NULL);
}

pthread_t setup_listener(config_t * config)
{
    pthread_t server_tid = 0;

    if (!socketize(config->argv[0], listener, IPPROTO_UDP, RELAY_CONN_IS_INBOUND, "listener"))
	DIE_RC(EXIT_FAILURE, "Failed to socketize listener");

    listener->polling_interval_millisec = config->polling_interval_millisec;

    /* must open the socket BEFORE we create the worker pool */
    open_socket(listener, DO_BIND | DO_REUSEADDR | DO_EPOLLFD, 0, config->server_socket_rcvbuf_bytes);

    /* create worker pool /after/ we open the socket, otherwise we
     * might leak worker threads. */

    if (listener->proto == IPPROTO_UDP)
	spawn(&server_tid, udp_server, listener, PTHREAD_CREATE_JOINABLE);
    else
	spawn(&server_tid, tcp_server, listener, PTHREAD_CREATE_JOINABLE);

    return server_tid;
}

static struct graphite_config *graphite_config_clone(const struct graphite_config *old_config)
{
    struct graphite_config *new_config = (struct graphite_config *) malloc(sizeof(*new_config));
    memset(new_config, 0, sizeof(*new_config));
    new_config->addr = strdup(old_config->addr);
    new_config->target = strdup(old_config->target);
    new_config->send_interval_millisec = old_config->send_interval_millisec;
    new_config->sleep_poll_interval_millisec = old_config->sleep_poll_interval_millisec;
    return new_config;
}

static int graphite_config_changed(const struct graphite_config *old_config, const struct graphite_config *new_config)
{
    return
	STRNE(old_config->addr, new_config->addr) ||
	STRNE(old_config->target, new_config->target) ||
	old_config->send_interval_millisec != new_config->send_interval_millisec ||
	old_config->sleep_poll_interval_millisec != new_config->sleep_poll_interval_millisec;
}

static void graphite_config_destroy(struct graphite_config *config)
{
    assert(config->addr);
    assert(config->target);
    free(config->addr);
    free(config->target);
    /* Reset the config to trap use-after-free. */
    memset(config, 0, sizeof(*config));
    free(config);
}

static int serve(config_t * config)
{
    pthread_t server_tid = 0;

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, sig_handler);
    signal(SIGHUP, sig_handler);

    setproctitle("starting");

    listener = calloc_or_die(sizeof(*listener));

    worker_pool_init_static(config);
    server_tid = setup_listener(config);
    graphite_worker = graphite_worker_create(config);
    pthread_create(&graphite_worker->base.tid, NULL, graphite_worker_thread, graphite_worker);

    fixed_buffer_t *process_status_buffer = fixed_buffer_create(PROCESS_STATUS_BUF_LEN);

    control_set_bits(RELAY_RUNNING);

    for (;;) {
	uint32_t control = control_get_bits();
	if (control & RELAY_STOPPING) {
	    WARN("Stopping");
	    break;
	} else if (control & RELAY_RELOADING) {
	    WARN("Reloading");
	    struct graphite_config *old_graphite_config = graphite_config_clone(&config->graphite);
	    if (config_reload(config)) {
		SAY("Reloading the listener and worker pool");
		stop_listener(server_tid);
		server_tid = setup_listener(config);
		worker_pool_reload_static(config);
		SAY("Reloaded the listener and worker pool");
		if (graphite_config_changed(old_graphite_config, &config->graphite)) {
		    SAY("Graphite config changed, reloading the graphite worker");
		    graphite_worker_destroy(graphite_worker);
		    graphite_worker = graphite_worker_create(config);
		    pthread_create(&graphite_worker->base.tid, NULL, graphite_worker_thread, graphite_worker);
		    SAY("Reloaded the graphite worker");
		} else {
		    SAY("Graphite config unchanged, not reloading the graphite worker");
		}
	    }
	    graphite_config_destroy(old_graphite_config);
	    control_unset_bits(RELAY_RELOADING);
	}

	update_process_status(process_status_buffer,
			      RELAY_ATOMIC_READ(RECEIVED_STATS.received_count),
			      RELAY_ATOMIC_READ(RECEIVED_STATS.tcp_connections));
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
	control_set_bits(RELAY_RELOADING);
	break;
    case SIGTERM:
    case SIGINT:
	control_set_bits(RELAY_STOPPING);
	break;
    default:
	WARN("IGNORE: Received unexpected signal %d", signum);
    }
}

static void stop_listener(pthread_t server_tid)
{
    shutdown(listener->socket, SHUT_RDWR);
    /* TODO: if the relay is interrupted rudely (^C), final_shutdown()
     * is called, which will call stop_listener(), and this close()
     * triggers the ire of the clang threadsanitizer, since the socket
     * was opened by a worker thread with a recv() in udp_server, but
     * the shutdown happens in the main thread. */
    close(listener->socket);
    pthread_join(server_tid, NULL);
}

static void final_shutdown(pthread_t server_tid)
{
    graphite_worker_destroy(graphite_worker);
    stop_listener(server_tid);
    worker_pool_destroy_static();
    free(listener);
    sleep(1);			/* give a chance to the detachable tcp worker threads to pthread_exit() */
    config_destroy();
    destroy_proctitle();
}

int main(int argc, char **argv)
{
    control_set_bits(RELAY_STARTING);
    config_init(argc, argv);
    initproctitle(argc, argv);
    return serve(&CONFIG);
}
