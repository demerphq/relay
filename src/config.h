#ifndef RELAY_CONFIG_H
#define RELAY_CONFIG_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

extern const char *OUR_NAME;

struct graphite_config {
    /* host:port for sending data to graphite,
     * or a filename. */
    char *dest_addr;

    /* the full path will be either:
     *
     * root.com.bar.foo.metric
     * root.com.bar.foo.ports.metric
     *
     * where the root is this root, and the ports is added
     * if the add_ports is true.  If it is added, it will be
     * something like udp_0_0_0_0_1234.tcp_localhost_5678,
     * derived from the listener-worker pair. */
    char *path_root;
    int add_ports;

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

    /* if non-zero, log syslog also to stderr */
    int syslog_to_stderr;

    /* if zero, stay in foreground.  otherwise fork twice and
     * do the other usual daemony things. */
    int daemonize;

    /* directory for the config saves */
    char *config_save_root;
    /* if true, configs are saved */
    int config_save;

    /* lock file, will contain pid */
    char *lock_file;

    /* config filename itself */
    char *config_file;

    /* various time limits used for sending, etc */
    uint32_t tcp_send_timeout_millisec;
    uint32_t polling_interval_millisec;
    uint32_t sleep_after_disaster_millisec;

    uint32_t server_socket_rcvbuf_bytes;
    uint32_t server_socket_sndbuf_bytes;

    /* if disabled, we will just drop packets
     * we cannot send out in time (spill_millisec,
     * see also spill_grace_millisec)
     * DANGER DANGER DANGER */
    int spill_enabled;

    /* root directory for where we write failed sends,
     * and "spilled" data */
    char *spill_root;

    /* packets older than this are spilled/dropped,
     * should be more than tcp send timeout */
    uint32_t spill_millisec;

    /* the spill logic forgives this much if the socket backends are
     * not present, but once this much has passed, the spill/drop engages. */
    uint32_t spill_grace_millisec;

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

/* Note that these receive and send buffer default sizes
 * are usually way above what the operating systems actually
 * are willing to give.  You will get something less. */

#ifndef DEFAULT_SERVER_SOCKET_RCVBUF_BYTES
#define DEFAULT_SERVER_SOCKET_RCVBUF_BYTES (32 * 1024 * 1024)
#endif

#ifndef DEFAULT_SERVER_SOCKET_SNDBUF_BYTES
#define DEFAULT_SERVER_SOCKET_SNDBUF_BYTES (32 * 1024 * 1024)
#endif

#ifndef DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC
#define DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC 100
#endif

#ifndef DEFAULT_LOCK_FILE
#define DEFAULT_LOCK_FILE "/var/run/event-relay/event-relay.pid"
#endif

#ifndef DEFAULT_CONFIG_SAVE_ROOT
#define DEFAULT_CONFIG_SAVE_ROOT "/var/run/event-relay"
#endif

#ifndef DEFAULT_CONFIG_SAVE
#define DEFAULT_CONFIG_SAVE 1
#endif

#ifndef DEFAULT_SPILL_ENABLED
#define DEFAULT_SPILL_ENABLED 1
#endif

#ifndef DEFAULT_SPILL_MILLISEC
#define DEFAULT_SPILL_MILLISEC 3000
#endif

#ifndef DEFAULT_SPILL_GRACE_MILLISEC
#define DEFAULT_SPILL_GRACE_MILLISEC (20 * 1000)
#endif

#ifndef DEFAULT_SPILL_ROOT
#define DEFAULT_SPILL_ROOT "/var/tmp/event-relay/spill"
#endif

#ifndef DEFAULT_GRAPHITE_DEST_ADDR
#define DEFAULT_GRAPHITE_DEST_ADDR "/var/run/event-relay/event-relay.graphite"
#endif

#ifndef DEFAULT_GRAPHITE_PATH_ROOT
#define DEFAULT_GRAPHITE_PATH_ROOT "event-relay.per_ten"
#endif

#ifndef DEFAULT_GRAPHITE_ADD_PORTS
#define DEFAULT_GRAPHITE_ADD_PORTS 0
#endif

#ifndef DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC
#define DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC 10000
#endif

#ifndef DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC
#define DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC 500
#endif

int config_reload(config_t * config, const char *file, time_t now);
void config_set_defaults(config_t * config);
void config_init(int argc, char **argv);
void config_die_args(int argc, char **argv);
void config_destroy(config_t * config);

#endif				/* #ifndef RELAY_CONFIG_H */
