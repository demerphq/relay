#include "relay.h"
static void cleanup(int signum);
static void sig_warn(int signum);
static struct sock s_listen;

int main(int ac, char **av) {
    signal(SIGTERM,cleanup);
    signal(SIGINT,cleanup);
    signal(SIGPIPE,sig_warn);

    if (ac < 4)
        SAYX(EXIT_FAILURE,"%s local-host:local-port fallback-host:remote-port tcp@remote-host:remote-port ...",av[0]);

    socketize(av[1],&s_listen);
    if (s_listen.type != SOCK_DGRAM)
        SAYX(EXIT_FAILURE,"we can listen only on DGRAM sockets (udp/unix)");

    throttle_init_static();
    b_init_static();
    worker_init_static(ac - 2,&av[2]);

    open_socket(&s_listen,DO_BIND);

    ssize_t received;
#ifdef BLOB_ARRAY_DATA
    for (;;) {
        blob_t *b = b_new();
        received = recv(s_listen.socket,b->data,b->size,0);
        if (received > 0) {
            b_shift(b,received);
            ENQUEUE(b);
        } else {
            if (received < 0)
                SAYPX("recv");
            b_throw_in_garbage(b);
        }
    }
#else
    char buf[MAX_CHUNK_SIZE]; // unused, but makes recv() happy
    for (;;) { 
        received = recv(s_listen.socket,buf,MAX_CHUNK_SIZE,MSG_PEEK);
        if (received > 0) {
            blob_t *b = b_new();
            b_prepare(b,received);
            received = recv(s_listen.socket,b->data,received,0);
            if (received < 0)
                SAYPX("recv");
            b_shift(b,received);
            ENQUEUE(b);
        }
    }
#endif
    /* never reached */
    return(0);
}

static void sig_warn(int signum) {
    _E("IGNORE: unexpected signal %d",signum);
}

static void cleanup(int signum) {
    close(s_listen.socket);
    worker_destroy_static();
    b_destroy_static();

    if (s_listen.sa.in.sin_family == AF_UNIX)
        unlink(s_listen.sa.un.sun_path);

    exit(EXIT_SUCCESS);
}
