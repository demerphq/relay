#include "relay.h"
#include "worker.h"
#include "worker_pool.h"
#include "setproctitle.h"
#include "abort.h"
#include "config.h"
#include "timer.h"
#define MAX_BUF_LEN 128

static void sig_handler(int signum);
static void stop_listener(pthread_t server_tid);
static void final_shutdown(pthread_t server_tid);
sock_t *s_listen;
extern config_t CONFIG;
graphite_worker_t *graphite_worker;

stats_basic_counters_t RECEIVED_STATS= {
    .received_count= 0,       /* number of items we have received */
    .sent_count= 0,           /* number of items we have sent */
    .partial_count= 0,        /* number of items we have spilled */
    .spilled_count= 0,        /* number of items we have spilled */
    .error_count= 0,          /* number of items that had an error */
    .disk_count= 0,           /* number of items we have written to disk */

    .send_elapsed_usec=0,    /* elapsed time in microseconds that we spent sending data */
    .active_connections=0,   /* current number of active inbound tcp connections */
};


#define MAX_BUF_LEN 128
void mark_second_elapsed() {
    char str[MAX_BUF_LEN+1];
    stats_count_t received= RELAY_ATOMIC_READ(RECEIVED_STATS.received_count);
    stats_count_t active  = RELAY_ATOMIC_READ(RECEIVED_STATS.active_connections);

    /* set it in the process name */
    int wrote= snprintf(
        str, MAX_BUF_LEN,
        STATSfmt " ^" STATSfmt , received, active
    );

    add_worker_stats_to_ps_str(str + wrote, MAX_BUF_LEN - wrote);
    setproctitle(str);
}


static void spawn(pthread_t *tid,void *(*func)(void *), void *arg, int type) {
    pthread_t unused;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, type);
    pthread_create(tid ? tid : &unused, &attr, func, arg);
    pthread_attr_destroy(&attr);
}

static inline blob_t * buf_to_blob_enqueue(char *buf, size_t size) {
    blob_t *b;
    if (size == 0) {
        if (0)
            WARN("Received 0 byte packet, not forwarding.");
        return NULL;
    }

    RELAY_ATOMIC_INCREMENT( RECEIVED_STATS.received_count, 1 );
    b = b_new(size);
    memcpy(BLOB_BUF_addr(b), buf, size);
    enqueue_blob_for_transmission(b);
    return b;
}

void *udp_server(void *arg) {
    sock_t *s = (sock_t *) arg;
    ssize_t received;
#ifdef PACKETS_PER_SECOND
    uint32_t packets = 0, prev_packets = 0;
    uint32_t epoch, prev_epoch = 0;
#endif
    char buf[MAX_CHUNK_SIZE]; // unused, but makes recv() happy
    while (not_aborted()) {
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
        buf_to_blob_enqueue(buf,received);
    }
    WARN_ERRNO("recv failed");
    set_aborted();
    pthread_exit(NULL);
}
#define EXPECTED(x) (*(int *) &(x)->buf[0])

void *tcp_server(void *arg) {
    sock_t *s = (sock_t *) arg;
    int i,fd,try_to_read,received;
    struct tcp_client *clients,*client;
    struct pollfd *pfds = NULL;
    volatile nfds_t nfds;
    setnonblocking(s->socket);

    nfds = 1;
    pfds = mallocz_or_die(nfds * sizeof(struct pollfd));
    pfds->fd = s->socket;
    pfds->events = POLLIN;
    clients = NULL;
    RELAY_ATOMIC_AND( RECEIVED_STATS.active_connections, 0);
    int rc;
    for (;;) {
        rc = poll(pfds,nfds,CONFIG.polling_interval_ms);
        if (rc == -1) {
            if (rc == EINTR)
                continue;
            WARN_ERRNO("poll");
            goto out;
        }
        for (i = 0; i < nfds; i++) {
            if (!pfds[i].revents)
                continue;
            if (pfds[i].fd == s->socket) {
                fd = accept(s->socket, NULL, NULL);
                if (fd == -1) {
                    WARN_ERRNO("accept");
                    goto out;
                }
                setnonblocking(fd);
                RELAY_ATOMIC_INCREMENT( RECEIVED_STATS.active_connections, 1 );
                pfds = realloc_or_die(pfds, (nfds + 1) * sizeof(*pfds));
                clients = realloc_or_die(clients,(nfds + 1) * sizeof(*clients));

                clients[nfds].pos = 0;
                clients[nfds].buf = mallocz_or_die(ASYNC_BUFFER_SIZE);
//                WARN("[%d] CREATE %p fd: %d",i,clients[nfds].buf,fd);
                pfds[nfds].fd  = fd;
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                nfds++;
            } else {
                client = &clients[i];
                try_to_read = ASYNC_BUFFER_SIZE - client->pos; // try to read as much as possible
                if (try_to_read <= 0) {
                    WARN("disconnecting, try to read: %d, pos: %d", try_to_read, client->pos);
                    goto disconnect;
                }
                received = recv(pfds[i].fd, client->buf + client->pos, try_to_read,0);
                if (received <= 0) {
                    if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                        continue;

                disconnect:
                    shutdown(pfds[i].fd,SHUT_RDWR);
                    close(pfds[i].fd);
//                    WARN("[%d] DESTROY %p %d %d fd: %d vs %d",i,client->buf,client->x,i,pfds[i].fd,client->fd);
                    free(client->buf);

                    // shft left
                    memcpy(pfds + i,pfds + i + 1, (nfds - i - 1) * sizeof(struct pollfd));
                    memcpy(clients + i,clients + i + 1, (nfds - i - 1) * sizeof(struct tcp_client));

                    nfds--;
                    pfds = realloc_or_die(pfds, nfds * sizeof(struct pollfd));
                    clients = realloc_or_die(clients, nfds * sizeof(struct tcp_client));
                    RELAY_ATOMIC_DECREMENT( RECEIVED_STATS.active_connections, 1 );
                    continue;
                }
                client->pos += received;

            try_to_consume_one_more:

                if (client->pos < EXPECTED_HEADER_SIZE)
                    continue;

                if (EXPECTED(client) > MAX_CHUNK_SIZE) {
                    WARN("received frame (%d) > MAX_CHUNK_SIZE(%d)",EXPECTED(client),MAX_CHUNK_SIZE);
                    goto disconnect;
                }
                if (client->pos >= EXPECTED(client) + EXPECTED_HEADER_SIZE) {
                    buf_to_blob_enqueue(client->buf,EXPECTED(client));

                    client->pos -= EXPECTED(client) + EXPECTED_HEADER_SIZE;
                    if (client->pos < 0) {
                        WARN("BAD PACKET wrong 'next' position(< 0) pos: %d expected packet size:%d header_size: %d",
                             client->pos, EXPECTED(client),EXPECTED_HEADER_SIZE);
                        goto disconnect;
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
                        memmove(client->buf,
                                client->buf + EXPECTED_HEADER_SIZE + EXPECTED(client),
                                client->pos);
                        if (client->pos >= EXPECTED_HEADER_SIZE)
                            goto try_to_consume_one_more;
                    }
                }
            }
        }
    }
out:
    for (i = 0; i < nfds; i++) {
        if (pfds[i].fd != s->socket)
            free(clients[i].buf);
        shutdown(pfds[i].fd, SHUT_RDWR);
        close(pfds[i].fd);
    }
    free(pfds);
    free(clients);
    set_aborted();
    pthread_exit(NULL);
}

pthread_t setup_listener(config_t *config) {
    pthread_t server_tid= 0;

    socketize(config->argv[0], s_listen, IPPROTO_UDP, RELAY_CONN_IS_INBOUND, "listener" );

    /* must open the socket BEFORE we create the worker pool */
    open_socket(s_listen, DO_BIND|DO_REUSEADDR|DO_EPOLLFD, 0, config->server_socket_rcvbuf);

    /* create worker pool /after/ we open the socket, otherwise we
     * might leak worker threads. */

    if (s_listen->proto == IPPROTO_UDP)
        spawn(&server_tid, udp_server, s_listen, PTHREAD_CREATE_JOINABLE);
    else
        spawn(&server_tid, tcp_server, s_listen, PTHREAD_CREATE_JOINABLE);

    return server_tid;
}

int _main(config_t *config) {
    pthread_t server_tid= 0;

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, sig_handler);
    signal(SIGHUP, sig_handler);

    setproctitle("starting");

    s_listen = mallocz_or_die(sizeof(*s_listen));

    worker_pool_init_static(config);
    server_tid= setup_listener(config);
    graphite_worker= mallocz_or_die(sizeof(graphite_worker_t));
    pthread_create(&graphite_worker->tid, NULL, graphite_worker_thread, graphite_worker);

    for (;;) {
        int abort;

        abort= get_abort_val();
        if (abort & STOP) {
            break;
        }
        else
        if (abort & RELOAD) {
            if (config_reload(config)) {
                stop_listener(server_tid);
                server_tid= setup_listener(config);
                worker_pool_reload_static(config);
                /* XXX: check me */
                /* check and see if we need to stop the old graphite processor and replace it */
                graphite_worker_destroy(graphite_worker);
                pthread_create(&graphite_worker->tid, NULL, graphite_worker_thread, graphite_worker);
            }
            unset_abort_bits(RELOAD);
        }

        mark_second_elapsed();
        sleep(1);
    }
    final_shutdown(server_tid);
    SAY("bye");
    closelog();
    return(0);
}

static void sig_handler(int signum) {
    switch(signum) {
        case SIGHUP:
            set_abort_bits(RELOAD);
            break;
        case SIGTERM:
        case SIGINT:
            set_aborted();
            break;
        default:
            WARN("IGNORE: unexpected signal %d", signum);
    }
}

static void stop_listener(pthread_t server_tid) {
    shutdown(s_listen->socket, SHUT_RDWR);
    close(s_listen->socket);
    pthread_join(server_tid, NULL);
}

static void final_shutdown(pthread_t server_tid) {
    graphite_worker_destroy(graphite_worker);
    stop_listener(server_tid);
    worker_pool_destroy_static();
    free(s_listen);
    sleep(1); // give a chance to the detachable tcp worker threads to pthread_exit()
    config_destroy();
    destroy_proctitle();
}

int main(int argc, char **argv) {
    config_init(argc, argv);
    initproctitle(argc, argv);

    return _main(&CONFIG);
}
