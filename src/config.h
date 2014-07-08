#ifndef _CONFIG_H
#define _CONFIG_H
#define _GNU_SOURCE

#include <stdio.h>
struct config {
    char **argv;
    int argc;
    char *file;
    int polling_interval_ms;
    int sleep_after_disaster_ms;
    int tcp_send_timeout;
    int server_socket_rcvbuf;
    int max_pps;
    char *fallback_root;
};

void config_reload(void);
void config_init(int argc, char **argv);
void config_destroy(void);
#endif
