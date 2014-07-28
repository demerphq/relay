#ifndef _CONFIG_H
#define _CONFIG_H
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
struct config {
    /* original argv/argc, or synethisized from config file */
    char **argv;
    int argc;

    /* config filename itself */
    char *file;

    /* various meta data used for sending, etc */
    uint32_t spill_usec;
    int polling_interval_ms;
    int sleep_after_disaster_ms;
    int tcp_send_timeout;
    int server_socket_rcvbuf;
    int max_pps;

    /* root directory for where we write failed sends,
     * and "spilled" data */
    char *fallback_root;

    /* host:port for sending data to graphite */
    char *graphite_arg;
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

#ifndef DEFAULT_MAX_PPS
#define DEFAULT_MAX_PPS 0
#endif

#ifndef DEFAULT_SEND_TIMEOUT
#define DEFAULT_SEND_TIMEOUT    2
#endif

#ifndef DEFAULT_SERVER_SOCKET_RCVBUF
#define DEFAULT_SERVER_SOCKET_RCVBUF (32 * 1024 * 1024)
#endif

void config_reload(void);
void config_init(int argc, char **argv);
void config_destroy(void);
#endif
