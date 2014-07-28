#include "config.h"
#include "worker.h"

#include "log.h"

static const char *OUR_NAME= "event-relay";

config_t CONFIG;

void trim(char * s) {
    char *p = s;
    int l = strlen(p);

    while(isspace(p[l - 1])) p[--l] = 0;
    while(*p && isspace(*p)) ++p, --l;

    memmove(s, p, l + 1);
}

void config_reload(void) {
    if (!CONFIG.file)
        goto reset_default;
    FILE *f;
    char *line = NULL;
    ssize_t read;
    size_t len = 0;

    f = fopen(CONFIG.file, "r");
    if (f == NULL)
        DIE("fopen: %s",CONFIG.file);

    int i,tmp;
    for (i = 0; i < CONFIG.argc; i++)
        free(CONFIG.argv[i]);

    CONFIG.argc = 0;
    while ((read = getline(&line, &len, f)) != -1) {
        char *p;
        if ((p = strchr(line, '#')))
            *p = '\0';

        trim(line);

        if (strlen(line) != 0) {
            if ((p = strchr(line,'='))) {
                if (strlen(p) == 1)
                    DIE("bad config line: %s", line);
                *p = '\0';
                p++;
                if (strcmp("fallback_root", line) == 0) {
                    if (CONFIG.fallback_root && strcmp(CONFIG.fallback_root,p) != 0)
                        CONF_WARN("fallback_root","%s",CONFIG.fallback_root,p);
                    free(CONFIG.fallback_root);
                    CONFIG.fallback_root = strdup(p);
                } else if (strcmp("polling_interval_ms", line) == 0) {
                    CONFIG.polling_interval_ms = atoi(p);
                } else if (strcmp("sleep_after_disaster_ms", line) == 0) {
                    CONFIG.sleep_after_disaster_ms = atoi(p);
                } else if (strcmp("max_pps", line) == 0) {
                    CONFIG.max_pps = atoi(p);
                } else if (strcmp("tcp_send_timeout", line) == 0) {
                    tmp = atoi(p);
                    if (CONFIG.tcp_send_timeout && CONFIG.tcp_send_timeout != tmp)
                        CONF_WARN("tcp_send_timeout","%d",CONFIG.tcp_send_timeout,tmp);
                    CONFIG.tcp_send_timeout = tmp;
                } else if (strcmp("server_socket_rcvbuf", line) == 0) {
                    tmp = atoi(p);
                    if (CONFIG.server_socket_rcvbuf && CONFIG.server_socket_rcvbuf != tmp)
                        CONF_WARN("server_socket_rcvbuf","%d",CONFIG.server_socket_rcvbuf,tmp);
                    CONFIG.server_socket_rcvbuf = tmp;
                } else {
                    DIE("bad config option: %s",line);
                }
            } else {
                CONFIG.argv = realloc_or_die(CONFIG.argv, sizeof(line) * (CONFIG.argc + 1));
                CONFIG.argv[CONFIG.argc] = strdup(line);
                CONFIG.argc++;
            }
        }
    }
    fclose(f);
    if (line)
        free(line);
    SAY("loading config file %s", CONFIG.file);

reset_default:
    // assign some default settings

    if (!CONFIG.fallback_root)
        CONFIG.fallback_root = strdup(DEFAULT_FALLBACK_ROOT);
    if (CONFIG.polling_interval_ms <= 0)
        CONFIG.polling_interval_ms = DEFAULT_POLLING_INTERVAL_MS;
    if (CONFIG.sleep_after_disaster_ms <= 0)
        CONFIG.sleep_after_disaster_ms = DEFAULT_SLEEP_AFTER_DISASTER_MS;
    if (CONFIG.tcp_send_timeout <= 0)
        CONFIG.tcp_send_timeout = DEFAULT_SEND_TIMEOUT;
    if (CONFIG.server_socket_rcvbuf <= 0)
        CONFIG.server_socket_rcvbuf = DEFAULT_SERVER_SOCKET_RCVBUF;
    if (CONFIG.spill_usec <= 0)
        CONFIG.spill_usec = DEFAULT_SPILL_USEC;
    SAY("fallback_root: %s", CONFIG.fallback_root);
    SAY("polling_intraval_ms: %d", CONFIG.polling_interval_ms);
    SAY("sleep_after_disaster_ms: %d", CONFIG.sleep_after_disaster_ms);
    SAY("max_pps: %d", CONFIG.max_pps);
    SAY("tcp_send_timeout: %d", CONFIG.tcp_send_timeout);
    SAY("server_socket_rcvbuf: %d", CONFIG.server_socket_rcvbuf);
    SAY("spill_usec: %d", CONFIG.spill_usec);
}

void config_init(int argc, char **argv) {
    int i = 0;
    memset(&CONFIG,0,sizeof(CONFIG));

    if (argc == 2) {
        CONFIG.file = strdup(argv[1]);
    } else {
        CONFIG.file = NULL;
        CONFIG.argv = realloc_or_die(CONFIG.argv, sizeof(char *) * (argc));
        for (i=0; i < argc - 1; i++) {
            CONFIG.argv[i] = strdup(argv[i + 1]);
        }
        CONFIG.argc = i;
    }
    openlog(OUR_NAME, LOG_CONS | LOG_ODELAY | LOG_PID | LOG_PERROR, OUR_FACILITY);
    config_reload();
}

void config_destroy(void) {
    int i;
    for (i = 0; i < CONFIG.argc; i++)
        free(CONFIG.argv[i]);
    free(CONFIG.fallback_root);
    free(CONFIG.file);
}
