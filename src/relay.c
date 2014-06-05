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

int main(int ac, char **av) {
    signal(SIGTERM,cleanup);
    signal(SIGINT,cleanup);
    signal(SIGPIPE,sig_handler);
    signal(SIGHUP,sig_handler);

    load_config(ac,av);

    if (CONFIG.ac < 3)
        SAYX(EXIT_FAILURE,"%s local-host:local-port fallback-host:remote-port tcp@remote-host:remote-port ...\n"\
                          "or file with socket description per day like:\n"                                  \
                          "\tlocal-host:local-port\n"                                                           \
                          "\tfallback-host:remote-port\n"                                                       \
                          "\ttcp@remote-host:remote-port ...\n",av[0]);

    socketize(CONFIG.av[0],&s_listen);
    if (s_listen.type != SOCK_DGRAM)
        SAYX(EXIT_FAILURE,"we can listen only on DGRAM sockets (udp/unix)");

    b_init_static();
    reload_workers(0);

    open_socket(&s_listen,DO_BIND);

    ssize_t received;
    char buf[MAX_CHUNK_SIZE]; // unused, but makes recv() happy
    for (;;) { 
        received = recv(s_listen.socket,buf,MAX_CHUNK_SIZE,MSG_PEEK);
        if (received > 0) {
            blob_t *b = b_new();
            b_prepare(b,received);
            received = recv(s_listen.socket,b->ref->data->data,received,0);
            if (received < 0)
                SAYPX("recv");
            enqueue_blob_for_transmission(b);
        }
    }
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
