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

static sock_t *s_listen;
extern config_t CONFIG;

stats_basic_counters_t RECEIVED_STATS= {
    .received_count= 0,       /* number of items we have received */
    .sent_count= 0,           /* number of items we have sent */
    .partial_count= 0,        /* number of items we have spilled */
    .spilled_count= 0,        /* number of items we have spilled */
    .error_count= 0,          /* number of items that had an error */
    .disk_count= 0,           /* number of items we have written to disk */

    .send_elapsed_usec=0,    /* elapsed time in microseconds that we spent sending data */
};


#define MAX_BUF_LEN 128
void mark_second_elapsed() {
    char str[MAX_BUF_LEN+1];
    stats_count_t received= RELAY_ATOMIC_READ(RECEIVED_STATS.received_count);

    /* set it in the process name */
    int wrote= snprintf(
        str, MAX_BUF_LEN,
        STATSfmt " ", received
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

static inline int recv_and_enqueue( int fd, int expected, int flags ) {
    int rc;
    blob_t *b;
    if ( expected == 0 ) {
        /* ignore 0 byte packets */
        if (0)
            WARN("Received 0 byte packet, not forwarding.");
    } else {
        RELAY_ATOMIC_INCREMENT( RECEIVED_STATS.received_count, 1 );
        b = b_new( expected );
        rc = recv( fd, BLOB_BUF_addr(b), expected, flags );
        if ( rc != BLOB_BUF_SIZE(b) ) {
            WARN_ERRNO("failed to receve packet payload, expected: %d got: %d", BLOB_BUF_SIZE(b), rc);
            b_destroy(b);
            return 0;
        }
        enqueue_blob_for_transmission(b);
    }
    return 1;
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
        received = recv(s->socket, buf, MAX_CHUNK_SIZE, MSG_PEEK);
#ifdef PACKETS_PER_SECOND
        if ((epoch = time(0)) != prev_epoch) {
            SAY("packets: %d", packets - prev_packets);
            prev_epoch = epoch;
            prev_packets = packets;
        }
        packets++;
#endif
        if (received < 0 || !recv_and_enqueue(s->socket,received,0)) {
            break;
        }
    }
    WARN_ERRNO("recv failed");
    set_aborted();
    pthread_exit(NULL);
}

void *tcp_server(void *arg) {
    sock_t *s = (sock_t *) arg;
    int i,nfds,fd,expected,try_to_read,received;
    blob_t *b;
    struct epoll_event ev, events[MAX_EVENTS];
    struct tcp_client *client;


    setnonblocking(s->socket);

    ev.events = EPOLLIN;
    ev.data.ptr = s;

    if (epoll_ctl(s->epollfd, EPOLL_CTL_ADD, s->socket, &ev) == -1)
        DIE("epoll_ctl failed on the listening socket");

    for (;;) {
        nfds = epoll_wait(s->epollfd, events, MAX_EVENTS, CONFIG.polling_interval_ms);
        if (nfds == -1) {
            if (nfds == EINTR) // timeout
                continue;
            WARN_ERRNO("epoll_wait");
            goto out;
        }
        for (i = 0; i < nfds; i++) {
            if (events[i].data.ptr == s) {
                fd = accept(s->socket, NULL, NULL);
                if (fd == -1) {
                    WARN_ERRNO("accept");
                    goto out;
                }
                setnonblocking(fd);
                client = mallocz_or_die(sizeof(*client));
                client->fd = fd;
                client->pos = 0;
                ev.data.ptr = client;
                ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
                if (epoll_ctl(s->epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                    WARN_ERRNO("epoll_ctl failed on client socket");
                    goto out;
                }
            } else {
                client = (struct tcp_client *) ev.data.ptr;
                if (events[i].events & EPOLLIN) {
                    // in case we have 2 packets in the same read(), we just consume one, and will shift the leftover buf to the left
                    // and just attempt to read again, if we need more bytes it will just return EAGAIN
                    // if it has the needed size it will be consumed, and again shifted to the left (in case we have 3 packets in one recv())
                    // if we have a header, we know what to expect, otherwise just get as much as possible in one syscall
                    #define EXPECTED(x) (*(uint32_t *) (x)->buf)
                    try_to_read = MAX_CHUNK_SIZE + EXPECTED_HEADER_SIZE - client->pos; // try to read as much as possible
                    if (try_to_read <= 0) {
                        WARN("disconnecting, try to read: %d, pos: %d",try_to_read,client->pos);
                        goto disconnect;
                    }
                    received = recv(client->fd, client->buf + client->pos, try_to_read,0);
                    if (received <= 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            continue;
                        WARN("disconnecting received: %d",received);
                        goto disconnect;
                    }
                    client->pos += received;
                try_to_consume_one_more:
                    if (client->pos < EXPECTED_HEADER_SIZE)
                        continue;
                    expected = EXPECTED(client);
                    if (expected == 0) {
                        client->pos = 0;
                        if (0)
                            WARN("Received 0 byte packet, not forwarding.");
                        continue;
                    }
                    if (expected > MAX_CHUNK_SIZE) {
                        WARN("received frame (%d) > MAX_CHUNK_SIZE(%d)",expected,MAX_CHUNK_SIZE);
                        goto disconnect;
                    }
                    if (client->pos >= expected) {

                        RELAY_ATOMIC_INCREMENT( RECEIVED_STATS.received_count, 1 );
                        b = b_new( expected );
                        memcpy(BLOB_BUF_addr(b), client->buf + EXPECTED_HEADER_SIZE,expected);
                        enqueue_blob_for_transmission(b);
                        client->pos -= expected + EXPECTED_HEADER_SIZE;
//                        WARN("COPY %d, pos: %d, opos: %d",expected,client->pos,client->pos + expected + EXPECTED_HEADER_SIZE);
                        if (client->pos > 0) {
                            memmove(client->buf,client->buf + expected + EXPECTED_HEADER_SIZE, client->pos);
                            client->pos = 0;
                            goto try_to_consume_one_more;
                        }
                    }
                } else {
                disconnect:
                    WARN("disconnecting");
                    shutdown(client->fd,SHUT_RDWR);
                    close(client->fd);
                    free(client);
                }
            }
        }
    }
out:
    close(s->socket);
    close(s->epollfd);
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
    close(s_listen->epollfd);
    pthread_join(server_tid, NULL);
}

static void final_shutdown(pthread_t server_tid) {
    stop_listener(server_tid);
    worker_pool_destroy_static();
    free(s_listen);
    sleep(1); // give a chance to the detachable tcp worker threads to pthread_exit()
    config_destroy();
}

int main(int argc, char **argv) {
    config_init(argc, argv);
    initproctitle(argc, argv);

    return _main(&CONFIG);
}
