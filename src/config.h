#ifndef _CONFIG_H
#define _CONFIG_H
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
struct config {
    /* original argc/argv, or synethisized from config file */
    int argc;
    char **argv;

    /* config filename itself */
    char *file;

    /* various meta data used for sending, etc */
    int polling_interval_ms;
    int sleep_after_disaster_ms;
    int max_pps;
    int tcp_send_timeout;
    int server_socket_rcvbuf;
    uint32_t spill_usec;
    int syslog_to_stderr;

    /* root directory for where we write failed sends,
     * and "spilled" data */
    char *fallback_root;

    /* host:port for sending data to graphite */
    char *graphite_arg;
    char *graphite_root;
    uint32_t graphite_send_interval_ms;
    uint32_t graphite_sleep_poll_interval_ms;

};

typedef struct config config_t;


#ifndef DEFAULT_SPILL_USEC
#define DEFAULT_SPILL_USEC 1000000
#endif

#ifndef DEFAULT_SLEEP_AFTER_DISASTER_MS
#define DEFAULT_SLEEP_AFTER_DISASTER_MS 1000
#endif

#ifndef DEFAULT_POLLING_INTERVAL_MS
#define DEFAULT_POLLING_INTERVAL_MS 1
#endif

#ifndef DEFAULT_FALLBACK_ROOT
#define DEFAULT_FALLBACK_ROOT "/tmp"
#endif

#ifndef DEFAULT_SEND_TIMEOUT
#define DEFAULT_SEND_TIMEOUT    2
#endif

#ifndef DEFAULT_SERVER_SOCKET_RCVBUF
#define DEFAULT_SERVER_SOCKET_RCVBUF (32 * 1024 * 1024)
#endif

#ifndef DEFAULT_GRAPHITE_ARG
#define DEFAULT_GRAPHITE_ARG "/tmp/relay.graphite"
#endif

#ifndef DEFAULT_GRAPHITE_ROOT
#define DEFAULT_GRAPHITE_ROOT "general.event-relay"
#endif

#ifndef DEFAULT_GRAPHITE_SEND_INTERVAL_MS
#define DEFAULT_GRAPHITE_SEND_INTERVAL_MS 60000
#endif

#ifndef DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MS
#define DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MS 500
#endif

#ifndef DEFAULT_SYSLOG_TO_STDERR
#define DEFAULT_SYSLOG_TO_STDERR 0
#endif

int config_reload(config_t *config);
void config_set_defaults(config_t *config);
void config_init(int argc, char **argv);
void config_die_args(int argc, char **argv);
void config_destroy(void);
#endif
