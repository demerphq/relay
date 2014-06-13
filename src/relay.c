#include "relay.h"
#include "worker.h"
#include "setproctitle.h"
#include "abort.h"
#include "config.h"
#define MAX_BUF_LEN 128
static void cleanup();
static void sig_handler(int signum);
static sock_t *s_listen;
extern struct config CONFIG;
stats_basic_counters_t RECEIVED_STATS= {
    .count= 0,
    .total= 0,
    .per_second= 0,
};

void inc_received_count() {
    inc_stats_count(&RECEIVED_STATS);
}

#define MAX_BUF_LEN 128
void mark_second_elapsed() {
    char str[MAX_BUF_LEN+1];
    stats_count_t received_total;
    stats_count_t received= snapshot_stats(&RECEIVED_STATS, &received_total);
    /* set it in the process name */
    int wrote= snprintf(
        str, MAX_BUF_LEN,
        STATSfmt " : " STATSfmt,
        received, received_total );

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


void reload_workers(int reload) {
    worker_init_static(CONFIG.argc - 1, &CONFIG.argv[1], reload);
}

static inline int recv_and_enqueue(int fd, int expected, int flags) {
    int rc;
    blob_t *b;
    if (expected == 0) {
        /* ignore 0 byte packets */
        if (0)
            WARN("Received 0 byte packet, not forwarding.");
    } else {
        inc_received_count();
        b = b_new(expected);
        rc = recv(fd, BLOB_BUF_addr(b), expected, flags);
        if (rc != BLOB_BUF_SIZE(b)) {
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

void *tcp_worker(void *arg) {
    int fd = (int )arg;
    SAY("new tcp worker for fd: %d", fd);
    while (not_aborted()) {
        uint32_t expected;
        int rc = recv(fd, &expected, sizeof(expected), MSG_WAITALL);
        if (rc != sizeof(expected)) {
            WARN_ERRNO("failed to receive header for next packet, expected: %zu got: %d", sizeof(expected), rc);
            break;
        }

        if (expected > MAX_CHUNK_SIZE) {
            WARN_ERRNO("requested packet(%d) > MAX_CHUNK_SIZE(%d)", expected, MAX_CHUNK_SIZE);
            break;
        }

        if (!recv_and_enqueue(fd,expected,MSG_WAITALL)) {
            break;
        }
    }
    close(fd);
    pthread_exit(NULL);
}

void *tcp_server(void *arg) {
    sock_t *s = (sock_t *) arg;

    for (;;) {
        int fd = accept(s->socket, NULL, NULL);
        if (fd < 0) {
            set_aborted();
            WARN_ERRNO("accept");
            break;
        }
        spawn(NULL, tcp_worker, (void *)fd, PTHREAD_CREATE_DETACHED);
    }

    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    pthread_t server_tid;

    config_init(argc, argv);

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, sig_handler);
    signal(SIGHUP, sig_handler);


    initproctitle(argc, argv);
    setproctitle("starting");

    if (CONFIG.argc < 2)
        DIE_RC(EXIT_FAILURE, "%s local-host:local-port tcp@remote-host:remote-port ...\n"       \
                          "or file with socket description per day like:\n"                     \
                          "\tlocal-host:local-port\n"                                           \
                          "\ttcp@remote-host:remote-port ...\n", argv[0]);
    s_listen = malloc_or_die(sizeof(*s_listen));
    socketize(CONFIG.argv[0], s_listen);

    /* must open the socket BEFORE we create the worker pool */
    open_socket(s_listen, DO_BIND, 0, CONFIG.server_socket_rcvbuf);

    /* create worker pool /after/ we open the socket, otherwise we
     * might leak worker threads. */
    reload_workers(0);

    if (s_listen->proto == IPPROTO_UDP)
        spawn(&server_tid, udp_server, s_listen, PTHREAD_CREATE_JOINABLE);
    else
        spawn(&server_tid, tcp_server, s_listen, PTHREAD_CREATE_JOINABLE);

    for (;;) {
        int abort= get_abort_val();
        if (abort & STOP) {
            break;
        }
        else
        if (abort & RELOAD) {
            config_reload();
            reload_workers(1);
            unset_abort_bits(RELOAD);
        }
        mark_second_elapsed();
        sleep(1);
    }
    cleanup(server_tid);
    SAY("bye");
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

static void cleanup(pthread_t server_tid) {
    shutdown(s_listen->socket, SHUT_RDWR);
    close(s_listen->socket);
    pthread_join(server_tid, NULL);
    worker_destroy_static();
    disk_writer_stop();
    free(s_listen);
    sleep(1); // give a chance to the detachable tcp worker threads to pthread_exit()
    config_destroy();
}
