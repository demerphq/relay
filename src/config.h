#ifndef RELAY_CONFIG_H
#define RELAY_CONFIG_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

extern const char *OUR_NAME;

struct graphite_config {
    /* host:port for sending data to graphite,
     * or a filename. */
    char *addr;
    /* foo.bar.val - "graphite_path" would
     * be a bit ambiguous */
    char *target;
    uint32_t send_interval_millisec;
    uint32_t sleep_poll_interval_millisec;
};

struct config {
    /* original argc/argv, or synthesized from config file */
    int argc;
    char **argv;

    uint64_t generation;
    time_t epoch_attempt;
    time_t epoch_changed;
    time_t epoch_success;

    /* config filename itself */
    char *config_file;

    /* various meta data used for sending, etc */
    int polling_interval_millisec;
    int sleep_after_disaster_millisec;
    int max_pps;		/* XXX unused */
    int tcp_send_timeout_millisec;
    int server_socket_rcvbuf_bytes;
    int server_socket_sndbuf_bytes;
    uint32_t spill_millisec;
    int syslog_to_stderr;
    int daemonize;

    /* root directory for where we write failed sends,
     * and "spilled" data */
    char *spill_root;

    struct graphite_config graphite;
};

typedef struct config config_t;

#ifndef DEFAULT_SYSLOG_TO_STDERR
#define DEFAULT_SYSLOG_TO_STDERR 0
#endif

#ifndef DEFAULT_DAEMONIZE
#define DEFAULT_DAEMONIZE 1
#endif

#ifndef DEFAULT_POLLING_INTERVAL_MILLISEC
#define DEFAULT_POLLING_INTERVAL_MILLISEC 1
#endif

#ifndef DEFAULT_TCP_SEND_TIMEOUT_MILLISEC
#define DEFAULT_TCP_SEND_TIMEOUT_MILLISEC 1000
#endif

#ifndef DEFAULT_SERVER_SOCKET_RCVBUF_BYTES
#define DEFAULT_SERVER_SOCKET_RCVBUF_BYTES (32 * 1024 * 1024)
#endif

#ifndef DEFAULT_SERVER_SOCKET_SNDBUF_BYTES
#define DEFAULT_SERVER_SOCKET_SNDBUF_BYTES (32 * 1024 * 1024)
#endif

#ifndef DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC
#define DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC 100
#endif

#ifndef DEFAULT_SPILL_MILLISEC
#define DEFAULT_SPILL_MILLISEC 1000
#endif

#ifndef DEFAULT_SPILL_ROOT
#define DEFAULT_SPILL_ROOT "/var/log/events/spill"
#endif

#ifndef DEFAULT_GRAPHITE_ADDR
#define DEFAULT_GRAPHITE_ADDR "/tmp/event_relay.graphite"
#endif

#ifndef DEFAULT_GRAPHITE_TARGET
#define DEFAULT_GRAPHITE_TARGET "general.event_relay"
#endif

#ifndef DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC
#define DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC 10000
#endif

#ifndef DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC
#define DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC 500
#endif

int config_reload(config_t * config, const char *file);
void config_set_defaults(config_t * config);
void config_init(int argc, char **argv);
void config_die_args(int argc, char **argv);
void config_destroy(void);

#endif				/* #ifndef RELAY_CONFIG_H */
