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
    ssize_t received;
#ifdef PACKETS_PER_SECOND
    uint32_t packets = 0, prev_packets = 0;
    uint32_t epoch, prev_epoch = 0;
#endif
    char buf[MAX_CHUNK_SIZE];	// unused, but makes recv() happy
    while (not_stopped()) {
	received = recv(s->socket, buf, MAX_CHUNK_SIZE, 0);
#ifdef PACKETS_PER_SECOND
	if ((epoch = time(0)) != prev_epoch) {
	    SAY("packets: %d", packets - prev_packets);
	    prev_epoch = epoch;
	    prev_packets = packets;
	}
	packets++;
#endif
	if (received < 0)
	    break;
	buf_to_blob_enqueue(buf, received);
    }
    WARN_ERRNO("recv failed");
    set_stopped();
    pthread_exit(NULL);
}

#define EXPECTED(x) (*(int *) &(x)->buf[0])

typedef struct {
    struct pollfd *pfds;
    volatile nfds_t nfds;
    struct tcp_client *clients;
} tcp_server_context_t;

static INLINE void tcp_context_realloc(tcp_server_context_t * ctxt, nfds_t n)
{
    ctxt->pfds = realloc_or_die(ctxt->pfds, n * sizeof(struct pollfd));
    ctxt->clients = realloc_or_die(ctxt->clients, n * sizeof(struct tcp_client));
}

void tcp_disconnect(tcp_server_context_t * ctxt, int i)
{
    /* We could pass in both client and i, but then there's danger of mismatch. */
    struct tcp_client *client = &ctxt->clients[i];

    shutdown(ctxt->pfds[i].fd, SHUT_RDWR);
    close(ctxt->pfds[i].fd);
    // WARN("[%d] DESTROY %p %d %d fd: %d vs %d", i, client->buf, client->x, i, ctxt->pfds[i].fd, client->fd);
    free(client->buf);

    // shift left
    memcpy(ctxt->pfds + i, ctxt->pfds + i + 1, (ctxt->nfds - i - 1) * sizeof(struct pollfd));
    memcpy(ctxt->clients + i, ctxt->clients + i + 1, (ctxt->nfds - i - 1) * sizeof(struct tcp_client));

    ctxt->nfds--;
    tcp_context_realloc(ctxt, ctxt->nfds);
    RELAY_ATOMIC_DECREMENT(RECEIVED_STATS.active_connections, 1);
}

void *tcp_server(void *arg)
{
    sock_t *s = (sock_t *) arg;
    int i, fd, try_to_read, received;
    tcp_server_context_t ctxt;

    setnonblocking(s->socket);

    ctxt.nfds = 1;
    ctxt.pfds = calloc_or_die(ctxt.nfds * sizeof(struct pollfd));
    ctxt.pfds[0].fd = s->socket;
    ctxt.pfds[0].events = POLLIN;
    ctxt.clients = NULL;
    RELAY_ATOMIC_AND(RECEIVED_STATS.active_connections, 0);

    for (;;) {
	int rc = poll(ctxt.pfds, ctxt.nfds, CONFIG.polling_interval_ms);
	if (rc == -1) {
	    if (rc == EINTR)
		continue;
	    WARN_ERRNO("poll");
	    goto out;
	}
	for (i = 0; i < (int) ctxt.nfds; i++) {
	    if (!ctxt.pfds[i].revents)
		continue;
	    if (ctxt.pfds[i].fd == s->socket) {
		fd = accept(s->socket, NULL, NULL);
		if (fd == -1) {
		    WARN_ERRNO("accept");
		    goto out;
		}
		setnonblocking(fd);
		RELAY_ATOMIC_INCREMENT(RECEIVED_STATS.active_connections, 1);

		tcp_context_realloc(&ctxt, ctxt.nfds + 1);

		ctxt.clients[ctxt.nfds].pos = 0;
		ctxt.clients[ctxt.nfds].buf = calloc_or_die(ASYNC_BUFFER_SIZE);
		// WARN("[%d] CREATE %p fd: %d", i, ctxt.clients[ctxt.nfds].buf, fd);
		ctxt.pfds[ctxt.nfds].fd = fd;
		ctxt.pfds[ctxt.nfds].events = POLLIN;
		ctxt.pfds[ctxt.nfds].revents = 0;
		ctxt.nfds++;
	    } else {
		struct tcp_client *client = &ctxt.clients[i];
		try_to_read = ASYNC_BUFFER_SIZE - client->pos;	// try to read as much as possible
		if (try_to_read <= 0) {
		    WARN("disconnecting, try to read: %d, pos: %d", try_to_read, client->pos);
		    tcp_disconnect(&ctxt, i);
		    continue;
		}
		received = recv(ctxt.pfds[i].fd, client->buf + client->pos, try_to_read, 0);
		if (received <= 0) {
		    if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			continue;

		    tcp_disconnect(&ctxt, i);
		    continue;
		}
		client->pos += received;

	      try_to_consume_one_more:

		if (client->pos < EXPECTED_HEADER_SIZE)
		    continue;

		if (EXPECTED(client) > MAX_CHUNK_SIZE) {
		    WARN("received frame (%d) > MAX_CHUNK_SIZE(%d)", EXPECTED(client), MAX_CHUNK_SIZE);
		    tcp_disconnect(&ctxt, i);
		    continue;
		}
		if (client->pos >= EXPECTED(client) + EXPECTED_HEADER_SIZE) {
		    buf_to_blob_enqueue(client->buf, EXPECTED(client));

		    client->pos -= EXPECTED(client) + EXPECTED_HEADER_SIZE;
		    if (client->pos < 0) {
			WARN("BAD PACKET wrong 'next' position(< 0) pos: %d expected packet size:%d header_size: %d",
			     client->pos, EXPECTED(client), EXPECTED_HEADER_SIZE);
			tcp_disconnect(&ctxt, i);
			continue;
		    }
		    if (client->pos > 0) {
			// [ h ] [ h ] [ h ] [ h ] [ D ] [ D ] [ D ] [ h ] [ h ] [ h ] [ h ] [ D ]
			//                                                                     ^ pos(12)
			// after we remove the first packet + header it becomes:
			// [ h ] [ h ] [ h ] [ h ] [ D ] [ D ] [ D ] [ h ] [ h ] [ h ] [ h ] [ D ]
			//                           ^ pos (5)
			// and then we copy from header + data, to position 0, 5 bytes
			//
			// [ h ] [ h ] [ h ] [ h ] [ D ]
			//                           ^ pos (5)
			memmove(client->buf, client->buf + EXPECTED_HEADER_SIZE + EXPECTED(client), client->pos);
			if (client->pos >= EXPECTED_HEADER_SIZE)
			    goto try_to_consume_one_more;
		    }
		}
	    }
	}
    }

  out:
    for (i = 0; i < (int) ctxt.nfds; i++) {
	if (ctxt.pfds[i].fd != s->socket)
	    free(ctxt.clients[i].buf);
	shutdown(ctxt.pfds[i].fd, SHUT_RDWR);
	close(ctxt.pfds[i].fd);
    }
    free(ctxt.pfds);
    free(ctxt.clients);
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

int _main(config_t * config)
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
    sleep(1);			// give a chance to the detachable tcp worker threads to pthread_exit()
    config_destroy();
    destroy_proctitle();
}

int main(int argc, char **argv)
{
    config_init(argc, argv);
    initproctitle(argc, argv);

    return _main(&CONFIG);
}
