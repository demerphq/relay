#include "relay.h"
#include "worker.h"

static void cleanup(int signum);
static void sig_handler(int signum);
static struct sock s_listen;

struct config {
    char **av;
    int ac;
    char *file;
    pthread_mutex_t lock;
} CONFIG;

void trim(char * s) {
    char *p = s;
    int l = strlen(p);

    while(isspace(p[l - 1])) p[--l] = 0;
    while(*p && isspace(*p)) ++p, --l;

    memmove(s, p, l + 1);
}

void reload_config_file(void) {
    if (!CONFIG.file)
        return;
    FILE *f;
    char *line;
    ssize_t read;
    size_t len = 0;
    int i;

    f = fopen(CONFIG.file, "r");
    if (f == NULL)
        SAYPX("fopen");

    if (CONFIG.ac > 0) {
        for (i=0;i<CONFIG.ac;i++)
            free(CONFIG.av[i]);
    }

    CONFIG.ac = 0;
    while ((read = getline(&line, &len, f)) != -1) {
        char *p;
        if ((p = strchr(line,'#')))
            *p = '\0';

        trim(line);
        if (strlen(line) != 0) {
            CONFIG.av = realloc_or_die(CONFIG.av,sizeof(line) * (CONFIG.ac + 1));
            CONFIG.av[CONFIG.ac] = strdup(line);
            CONFIG.ac++;
        }
    }
    if (line)
        free(line);
    _D("loading config file %s",CONFIG.file);
}

void load_config(int ac, char **av) {
    int i = 0;
    CONFIG.av = NULL;
    CONFIG.ac = 0;
    CONFIG.file = NULL;
    if (ac == 2) {
        CONFIG.file = av[1];
        reload_config_file();
    } else {

        CONFIG.av = realloc_or_die(CONFIG.av,sizeof(char *) * (ac));
        for (i=0; i < ac - 1; i++) {
            CONFIG.av[i] = strdup(av[i + 1]);
        }
        CONFIG.ac = i;
    }
}

void reload_workers(int destroy) {
    worker_init_static(CONFIG.ac - 1,&CONFIG.av[1],destroy);
}

void udp_server(struct sock *s) {
    ssize_t received;
#ifdef PACKETS_PER_SECOND
    uint32_t packets = 0, prev_packets = 0;
    uint32_t epoch, prev_epoch = 0;
#endif
    char buf[MAX_CHUNK_SIZE]; // unused, but makes recv() happy
    for (;;) {
        received = recv(s->socket,buf,MAX_CHUNK_SIZE,MSG_PEEK);
#ifdef PACKETS_PER_SECOND
        if ((epoch = time(0)) != prev_epoch) {
            _D("packets: %d", packets - prev_packets);
            prev_epoch = epoch;
            prev_packets = packets;
        }
        packets++;
#endif
        if (received > 0) {
            blob_t *b = b_new();
            b_prepare(b,received);
            received = recv(s->socket,BLOB_DATA(b),received,0);
            if (received < 0)
                SAYPX("recv");
            enqueue_blob_for_transmission(b);
        }
    }
}

void *tcp_worker(void *arg) {
    int fd = (int )arg;
    _D("new tcp worker for fd: %d",fd);
    for (;;) {
        blob_t *b = b_new();
        uint32_t expected;
        int rc = recv(fd,&expected,sizeof(expected),MSG_WAITALL);
        if (rc != sizeof(expected)) {
            _ENO("failed to receive header for next packet, expected: %zu got: %d",sizeof(expected),rc);
            break;
        }

        if (expected > MAX_CHUNK_SIZE) {
            _ENO("requested packet(%d) > MAX_CHUNK_SIZE(%d)",expected,MAX_CHUNK_SIZE);
            break;
        }

        b_prepare(b,expected);
        rc = recv(fd,&BLOB_DATA(b),expected,MSG_WAITALL);
        if (rc != BLOB_SIZE(b)) {
            _ENO("failed to receve packet payload, expected: %d got: %d",BLOB_SIZE(b),rc);
            break;
        }
        enqueue_blob_for_transmission(b);
    }
    close(fd);
    pthread_exit(NULL);
}
void tcp_server(struct sock *s) {
    for (;;) {
        int fd = accept(s->socket,NULL,NULL);
        if (fd < 0)
            SAYPX("accept");
        pthread_attr_t attr;
        pthread_t t;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t,&attr,tcp_worker,(void *)fd);
        pthread_attr_destroy(&attr);
    }
}

int main(int ac, char **av) {
    signal(SIGTERM,cleanup);
    signal(SIGINT,cleanup);
    signal(SIGPIPE,sig_handler);
    signal(SIGHUP,sig_handler);

    load_config(ac,av);

    if (CONFIG.ac < 2)
        SAYX(EXIT_FAILURE,"%s local-host:local-port tcp@remote-host:remote-port ...\n" \
                          "or file with socket description per day like:\n"            \
                          "\tlocal-host:local-port\n"                                  \
                          "\ttcp@remote-host:remote-port ...\n",av[0]);

    socketize(CONFIG.av[0],&s_listen);
    b_init_static();
    reload_workers(0);
    open_socket(&s_listen,DO_BIND);
    if (s_listen.proto == IPPROTO_UDP)
        udp_server(&s_listen);
    else
        tcp_server(&s_listen);
    /* never reached */
    return(0);
}

static void sig_handler(int signum) {
    switch(signum) {
        case SIGHUP:
            reload_config_file();
            reload_workers(1);
            break;
        default:
            _E("IGNORE: unexpected signal %d",signum);
    }
}

static void cleanup(int signum) {
    close(s_listen.socket);
    worker_destroy_static();
    b_destroy_static();
    int i;
    for (i = 0; i < CONFIG.ac; i++)
        free(CONFIG.av[i]);

    if (s_listen.sa.in.sin_family == AF_UNIX)
        unlink(s_listen.sa.un.sun_path);

    exit(EXIT_SUCCESS);
}
