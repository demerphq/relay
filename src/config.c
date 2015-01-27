#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include "global.h"
#include "log.h"
#include "socket_worker.h"
#include "string_util.h"

const char *OUR_NAME = "event-relay";

void config_destroy(config_t * config)
{
    for (int i = 0; i < config->argc; i++)
        free(config->argv[i]);
    free(config->argv);
    free(config->graphite.dest_addr);
    free(config->graphite.path_root);
    free(config->config_save_root);
    free(config->spill_root);
    free(config->config_file);
    free(config->lock_file);
    free(config);
}

void config_set_defaults(config_t * config)
{
    if (config == NULL)
        return;

    config->syslog_to_stderr = DEFAULT_SYSLOG_TO_STDERR;
    config->daemonize = DEFAULT_DAEMONIZE;
    config->tcp_send_timeout_millisec = DEFAULT_TCP_SEND_TIMEOUT_MILLISEC;
    config->polling_interval_millisec = DEFAULT_POLLING_INTERVAL_MILLISEC;
    config->sleep_after_disaster_millisec = DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC;
    config->server_socket_rcvbuf_bytes = DEFAULT_SERVER_SOCKET_RCVBUF_BYTES;
    config->server_socket_sndbuf_bytes = DEFAULT_SERVER_SOCKET_SNDBUF_BYTES;
    config->max_socket_open_wait_millisec = DEFAULT_MAX_SOCKET_OPEN_WAIT_MILLISEC;

    config->lock_file = strdup(DEFAULT_LOCK_FILE);

    config->config_save_root = strdup(DEFAULT_CONFIG_SAVE_ROOT);
    config->config_save = DEFAULT_CONFIG_SAVE;

    config->spill_enabled = DEFAULT_SPILL_ENABLED;
    config->spill_millisec = DEFAULT_SPILL_MILLISEC;
    config->spill_grace_millisec = DEFAULT_SPILL_GRACE_MILLISEC;
    config->spill_root = strdup(DEFAULT_SPILL_ROOT);

    config->graphite.dest_addr = strdup(DEFAULT_GRAPHITE_DEST_ADDR);
    config->graphite.path_root = strdup(DEFAULT_GRAPHITE_PATH_ROOT);
    config->graphite.add_ports = DEFAULT_GRAPHITE_ADD_PORTS;
    config->graphite.send_interval_millisec = DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC;
    config->graphite.sleep_poll_interval_millisec = DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC;
}

static int is_non_empty_string(const char *s)
{
    return s && *s ? 1 : 0;
}

/* Accepts only ASCII paths: one or more 'words',
 * separated by single dots. */
static int is_valid_graphite_target(const char *path)
{
    if (!is_non_empty_string(path))
        return 0;
    const char *p;
    /* XXX This could be stricter */
    for (p = path; *p && (isalnum(*p) || *p == '_' || *p == '-'); p++) {
        while (isalnum(*p) || *p == '_' || *p == '-')
            p++;
        if (*p == 0)
            break;
        if (*p == '.' && isalnum(p[1]))
            continue;
        else if (*p) {
            return 0;
        }
    }
    return *p == 0;
}

static int is_valid_socketize(const char *arg, int default_proto, int connection_direction, const char *role)
{
    if (!is_non_empty_string(arg))
        return 0;
    /* NOTE: the result socketization is "lost" (beyond the success/failure)
     * and redone later when the listener and workers are started.  This may
     * be considered wasteful, but would get tricky on e.g. config reloads. */
    relay_socket_t s;
    if (!socketize(arg, &s, default_proto, connection_direction, role))
        return 0;
    return 1;
}

static int is_valid_directory(const char *path, int *saverr)
{
    if (is_non_empty_string(path)) {
        struct stat st;
        /* Yes, there's a race condition here. */
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return 1;
            } else {
                *saverr = ENOTDIR;
            }
        } else {
            *saverr = errno;
        }
    } else {
        *saverr = EINVAL;
    }
    return 0;
}

/* The upper limit is pretty arbitrary, but the basic idea is to
 * protect against too high values which indicate either mixup
 * of milli/micro, or overflows/wraparounds. */
#define MAX_SEC 60

static int is_valid_millisec(uint32_t millisec)
{
    return millisec > 0 && millisec <= MAX_SEC * 1000;
}

static int is_valid_buffer_size(uint32_t size)
{
    /* Pretty arbitrary choice but let's require alignment by 1048576,
     * which is also the minimum size. */
#define ONE_MEGABYTE (1024 * 1024)
    return (size >= ONE_MEGABYTE && ((size & (ONE_MEGABYTE - 1)) == 0));
}

#define CONFIG_VALID_STR(config, t, v, invalid)		\
    do { if (!t(config->v)) { WARN("%s value '%s' invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_DIRECTORY(config, v, invalid)		\
    do { int saverr; if (!is_valid_directory(config->v, &saverr)) { errno = saverr; WARN_ERRNO("%s value '%s' invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_SOCKETIZE(config, p, d, r, v, invalid)		\
    do { if (!is_valid_socketize(config->v, p, d, r " (config check)")) { WARN("%s value '%s' invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_NUM(config, t, v, invalid)		\
    do { if (!t(config->v)) { WARN("%s value %d invalid", #v, config->v); invalid++; } } while (0)

static int absolutize_config_file(config_t * config)
{
    if (config->config_file == NULL)
        return 0;
    if (config->config_file[0] == '/')
        return 1;
    else {
        char *buf = calloc_or_fatal(PATH_MAX);
        if (buf == NULL) {
            WARN("Failed to allocate absolute pathname");
        } else {
            if (getcwd(buf, PATH_MAX) == NULL) {
                WARN_ERRNO("Failed to getcwd");
            } else {
                size_t o = strlen(config->config_file);
                char *p = buf;
                size_t n;
                while (*p)
                    p++;
                n = p - buf;
                if (n + 1 /* slash */  + o > PATH_MAX) {
                    errno = ENAMETOOLONG;
                    WARN("Cannot append %s to %s", config->config_file, buf);
                } else {
                    *p++ = '/';
                    memcpy(p, config->config_file, o);
                    free(config->config_file);
                    config->config_file = buf;
                    SAY("Config file %s", buf);
                    return 1;
                }
            }
        }
        free(buf);
        return 0;
    }
}

static int config_valid_options(config_t * config)
{
    int invalid = 0;

    CONFIG_VALID_NUM(config, is_valid_millisec, tcp_send_timeout_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, polling_interval_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, sleep_after_disaster_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_buffer_size, server_socket_rcvbuf_bytes, invalid);
    CONFIG_VALID_NUM(config, is_valid_buffer_size, server_socket_sndbuf_bytes, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, max_socket_open_wait_millisec, invalid);

    CONFIG_VALID_STR(config, is_non_empty_string, lock_file, invalid);

    CONFIG_VALID_DIRECTORY(config, config_save_root, invalid);

    CONFIG_VALID_DIRECTORY(config, spill_root, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, spill_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, spill_grace_millisec, invalid);

    CONFIG_VALID_SOCKETIZE(config, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "graphite worker", graphite.dest_addr, invalid);
    CONFIG_VALID_STR(config, is_valid_graphite_target, graphite.path_root, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, graphite.send_interval_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, graphite.sleep_poll_interval_millisec, invalid);

    if (config->spill_millisec <= config->tcp_send_timeout_millisec) {
        WARN("spill_millisec %d should be more than tcp_send_timeout_millisec %d",
             config->spill_millisec, config->tcp_send_timeout_millisec);
        invalid++;
    }

    return invalid == 0;
}

static int config_valid_addresses(config_t * config)
{
    int invalid = 0;

    if (config->argc < 1) {
        WARN("Missing listener address");
        invalid++;
    } else {
        CONFIG_VALID_SOCKETIZE(config, IPPROTO_UDP, RELAY_CONN_IS_INBOUND, "listener", argv[0], invalid);
    }

    if (config->argc < 2) {
        WARN("Missing forward addresses");
        invalid++;
    } else {
        for (int i = 1; i < config->argc; i++) {
            CONFIG_VALID_SOCKETIZE(config, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "forward", argv[i], invalid);
        }
    }

    return invalid == 0;
}

static int config_valid_options_and_optional_addresses(config_t * config)
{
    int config_options_valid = config_valid_options(config);
    int config_addresses_missing_or_valid = (config->argc == 0) || config_valid_addresses(config);
    return config_options_valid && config_addresses_missing_or_valid;
}

#define TRY_OPT_BEGIN do
#define TRY_OPT_END   while (0)

#define TRY_NUM_OPT(name,line,p)                                            \
    if ( STREQ(#name, line) ) {                                             \
        char* endp;                                                         \
        long tmp = strtol(p, &endp, 10);		                    \
        if (*endp == 0)                                                     \
             config->name = tmp;                                            \
        else     							    \
	    WARN("Ignoring strange config value %s=%s", #name, p);	    \
        break;                                                              \
    }

#define TRY_STR_OPT(name,line,p)                                            \
    if ( STREQ(#name, line) ) {                                             \
        free(config->name);                                                 \
        config->name = strdup(p);                                           \
        break;                                                              \
    }

static int config_from_line(config_t * config, const char *line, char *copy, char **opt, char **val, int *line_num,
                            const char *file)
{
    char *p;

    (*line_num)++;

    /* End-of-line comment. */
    if ((p = strchr(copy, '#')))
        *p = '\0';

    trim_space(copy);

    *opt = NULL;
    *val = NULL;

    if (*copy) {
        *opt = copy;
        if ((p = strchr(copy, '='))) {
            if (p[1] == 0) {
                SAY("Error in config file %s:%d: %s", file, *line_num, line);
                return 0;
            }
            *p = '\0';
            p++;
            TRY_OPT_BEGIN {
                TRY_NUM_OPT(syslog_to_stderr, copy, p);
                TRY_NUM_OPT(daemonize, copy, p);

                /* Hack: line_num zero means argv. */
                if (*line_num == 0)
                    TRY_STR_OPT(config_file, copy, p);

                TRY_NUM_OPT(tcp_send_timeout_millisec, copy, p);
                TRY_NUM_OPT(polling_interval_millisec, copy, p);
                TRY_NUM_OPT(sleep_after_disaster_millisec, copy, p);
                TRY_NUM_OPT(server_socket_rcvbuf_bytes, copy, p);
                TRY_NUM_OPT(server_socket_sndbuf_bytes, copy, p);
                TRY_NUM_OPT(max_socket_open_wait_millisec, copy, p);

                TRY_STR_OPT(lock_file, copy, p);

                TRY_STR_OPT(config_save_root, copy, p);
                TRY_NUM_OPT(config_save, copy, p);

                TRY_NUM_OPT(spill_enabled, copy, p);
                TRY_STR_OPT(spill_root, copy, p);
                TRY_NUM_OPT(spill_millisec, copy, p);
                TRY_NUM_OPT(spill_grace_millisec, copy, p);

                TRY_STR_OPT(graphite.dest_addr, copy, p);
                TRY_STR_OPT(graphite.path_root, copy, p);
                TRY_NUM_OPT(graphite.add_ports, copy, p);
                TRY_NUM_OPT(graphite.send_interval_millisec, copy, p);
                TRY_NUM_OPT(graphite.sleep_poll_interval_millisec, copy, p);

                WARN("Error in config file %s:%d: bad config option: %s", file, *line_num, line);
                return 0;
            }
            TRY_OPT_END;
            *val = p;
        } else {
            config->argv = realloc_or_fatal(config->argv, sizeof(char *) * (config->argc + 1));
            if (config->argv == NULL)
                return 0;

            config->argv[config->argc] = strdup(copy);
            config->argc++;
        }
    }

    return 1;
}

static int config_to_buffer(const config_t * config, fixed_buffer_t * buf)
{
#define CONFIG_NUM_VCATF(name) \
    if (!fixed_buffer_vcatf(buf, #name " = %d\n", config->name)) return 0;
#define CONFIG_STR_VCATF(name) \
    if (!fixed_buffer_vcatf(buf, #name " = %s\n", config->name)) return 0;

    CONFIG_NUM_VCATF(syslog_to_stderr);
    CONFIG_NUM_VCATF(daemonize);
    CONFIG_NUM_VCATF(tcp_send_timeout_millisec);
    CONFIG_NUM_VCATF(polling_interval_millisec);
    CONFIG_NUM_VCATF(sleep_after_disaster_millisec);
    CONFIG_NUM_VCATF(server_socket_rcvbuf_bytes);
    CONFIG_NUM_VCATF(server_socket_sndbuf_bytes);
    CONFIG_NUM_VCATF(max_socket_open_wait_millisec);

    CONFIG_STR_VCATF(lock_file);

    CONFIG_STR_VCATF(config_save_root);
    CONFIG_NUM_VCATF(config_save);

    CONFIG_NUM_VCATF(spill_enabled);
    CONFIG_STR_VCATF(spill_root);
    CONFIG_NUM_VCATF(spill_millisec);
    CONFIG_NUM_VCATF(spill_grace_millisec);

    CONFIG_STR_VCATF(graphite.dest_addr);
    CONFIG_STR_VCATF(graphite.path_root);
    CONFIG_NUM_VCATF(graphite.add_ports);
    CONFIG_NUM_VCATF(graphite.send_interval_millisec);
    CONFIG_NUM_VCATF(graphite.sleep_poll_interval_millisec);

    for (int i = 0; i < config->argc; i++) {
        if (!fixed_buffer_vcatf(buf, "%s\n", config->argv[i]))
            return 0;
    }
    return 1;
}

void config_dump(config_t * config)
{
    if (config == NULL)
        return;
    else {
        fixed_buffer_t *buf = fixed_buffer_create(4096);

        config_to_buffer(config, buf);
        SAY("%s", buf->data);

        fixed_buffer_destroy(buf);
    }
}

static config_t *config_from_file(const char *file)
{
    if (file == NULL) {
        FATAL("Config file unknown");
        return NULL;
    }

    config_t *config = calloc_or_fatal(sizeof(config_t));
    if (config == NULL) {
        WARN("Failed to alloc config");
        return NULL;
    }

    config_set_defaults(config);

    SAY("Loading config file %s", file);

    int success = 0;
    int failure = 0;
    FILE *fp = fopen(file, "r");
    if (fp == NULL) {
        WARN_ERRNO("Failed to open: %s", file);
    } else {
        size_t len = 1024;
        char *line = calloc_or_fatal(len);
        if (line == NULL) {
            WARN("Failed to alloc line buffer");
        } else {
            int line_num = 0;
            char *opt, *val;
            while (getline(&line, &len, fp) != -1) {
                char *copy = strdup(line);
                if (config_from_line(config, line, copy, &opt, &val, &line_num, file)) {
                    success++;
                } else {
                    failure++;
                }
                free(copy);
            }
            free(line);
        }
        fclose(fp);
    }

    SAY("Loaded config file %s", file);

    if (!config_valid_options_and_optional_addresses(config)) {
        failure++;
    }

    if (failure) {
        config_dump(config);
        WARN("Invalid configuration");
        return NULL;
    }

    return config;
}

static int config_to_file(const config_t * config, int fd)
{
    fixed_buffer_t *buf = fixed_buffer_create(4096);
    int success = 0;

    if (buf) {
        if (config_to_buffer(config, buf)) {
            ssize_t wrote;
            if ((wrote = write(fd, buf->data, buf->used)) == buf->used) {
                if (fsync(fd) == 0) {
                    success = 1;
                } else {
                    WARN_ERRNO("fsync() failed");
                }
            } else {
                WARN_ERRNO("write() failed, tried writing %ld but wrote %ld", buf->size, wrote);
            }
        }
        fixed_buffer_destroy(buf);
    }

    if (!success)
        WARN("Failed to write config to file");

    return success;
}

static int config_save(const config_t * config, time_t now)
{
    if (config->config_file == NULL) {
        WARN("Cannot save config with NULL config_file");
        return 0;
    } else if (config->config_save_root == NULL) {
        WARN("Cannot save config with NULL config_save_root");
        return 0;
    } else {
        char buf[PATH_MAX];

        /* Find the basename of config_file,
         * but leave the pointer at the last slash. */
        char *p = config->config_file;
        while (*p)
            p++;
        while (p > config->config_file && *p != '/')
            p--;

        if (*p != '/' || p == config->config_file) {
            WARN("Config file %s looks odd", config->config_file);
            return 0;
        }

        char *dst = buf;
        char *src = config->config_save_root;
        while (dst - buf < PATH_MAX && *src) {
            *dst++ = *src++;
        }

        int room = PATH_MAX - (dst - buf);
        int wrote = snprintf(dst, room, "%s.save.%ld.%d", p, (long) now, (int) getpid());
        if (wrote < 0 || wrote >= room) {
            WARN("Failed to build config save name into %s from %s", config->config_save_root, config->config_file);
            return 0;
        }
        int fd = open(buf, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            WARN_ERRNO("Failed to open config save %s", buf);
            return 0;
        }
        if (!config_to_file(config, fd)) {
            WARN_ERRNO("Failed to save config to %s", buf);
            return 0;
        }
        if (close(fd) == -1) {
            WARN_ERRNO("Failed to close save config as %s", buf);
            return 0;
        }
        SAY("Saved config as %s", buf);
        return 1;
    }
}

#define IF_NUM_OPT_CHANGED(name,config,new_config)          \
  do { \
    if ( config->name != new_config->name ) {               \
        SAY("Changing '" #name "' from '%d' to '%d'",        \
                config->name, new_config->name);            \
        config->name = new_config->name;                    \
        config_changed = 1;                                 \
    } \
  } while(0)

#define IF_STR_OPT_CHANGED(name,config,new_config)          \
  do { \
    if ( STRNE(config->name, new_config->name) )       {    \
        SAY("Changing '" #name "' from '%s' to '%s'",        \
                config->name, new_config->name);            \
        free(config->name);                                 \
        config->name = new_config->name;                    \
        config_changed = 1;                                 \
    } else {						    \
	free(new_config->name);			    	    \
	new_config->name = NULL;		    	    \
    }						            \
  } while(0)

int config_reload(config_t * config, const char *file, time_t now)
{
    int config_changed = 0;
    int first_config = config->generation == 0;

    config->epoch_attempt = now;

    SAY("Config reload start: generation %ld epoch_attempt %ld epoch_changed %ld epoch_success %ld now %ld",
        (long) config->generation,
        (long) config->epoch_attempt, (long) config->epoch_changed, (long) config->epoch_success, (long) now);

    if (file != config->config_file) {
        free(config->config_file);      /* from option parsing */
        config->config_file = strdup(file);
    }
    if (!absolutize_config_file(config)) {
        WARN("Failed to absolutize config");
        return 0;
    }

    if (first_config) {
        SAY("Loading config file %s", config->config_file);
        config_changed = 1;
    } else {
        SAY("Reloading config file %s", config->config_file);
    }

    config_t *new_config = config_from_file(config->config_file);

    if (new_config == NULL) {
        if (config->generation) {
            SAY("Failed to reload config, not restarting");
        } else {
            /* This is the initial startup: if there's no config,
             * we should just die. */
            FATAL("Failed to load config, not starting");
        }
        return 0;
    }

    if (first_config) {
        SAY("Loaded config file %s", config->config_file);
        SAY("Initial config");
    } else {
        SAY("Reloaded config file %s", config->config_file);
        SAY("New unmerged config");
    }

    config_dump(new_config);

    if (!config_valid_options_and_optional_addresses(new_config)) {
        if (first_config)
            FATAL("Invalid initial configuration");
        else
            WARN("Invalid new configuration, ignoring");
        return 0;
    }

    if (config->generation)
        SAY("Merging new configuration with old");

    if (control_is(RELAY_STARTING)) {
        IF_NUM_OPT_CHANGED(daemonize, config, new_config);
    } else {
        WARN("Changing daemonize has no effect (has effect only on startup)");
    }

    IF_NUM_OPT_CHANGED(tcp_send_timeout_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(polling_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(sleep_after_disaster_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(server_socket_rcvbuf_bytes, config, new_config);
    IF_NUM_OPT_CHANGED(server_socket_sndbuf_bytes, config, new_config);

    if (control_is(RELAY_STARTING)) {
        IF_STR_OPT_CHANGED(lock_file, config, new_config);
    } else {
        WARN("Changing lock_file has no effect (has effect only on startup)");
    }

    IF_STR_OPT_CHANGED(config_save_root, config, new_config);
    IF_NUM_OPT_CHANGED(config_save, config, new_config);

    IF_NUM_OPT_CHANGED(spill_enabled, config, new_config);
    IF_STR_OPT_CHANGED(spill_root, config, new_config);
    IF_NUM_OPT_CHANGED(spill_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(spill_grace_millisec, config, new_config);

    IF_STR_OPT_CHANGED(graphite.dest_addr, config, new_config);
    IF_STR_OPT_CHANGED(graphite.path_root, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.add_ports, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.send_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.sleep_poll_interval_millisec, config, new_config);

    if (new_config->argc) {
        SAY("Listener or forwarder changes");
        for (int i = 0; i < config->argc; i++) {
            if (i < new_config->argc) {
                if (STRNE(config->argv[i], new_config->argv[i])) {
                    if (config->generation == 0) {
                        SAY("Setting %s socket config to '%s'", i == 0 ? "listen" : "forward", new_config->argv[i]);
                    } else {
                        SAY("Changing %s socket config from '%s' to '%s'",
                            i == 0 ? "listen" : "forward", config->argv[i], new_config->argv[i]);
                    }
                    config_changed = 1;
                }
            } else {
                SAY("Stopping forward socket to '%s'", config->argv[i]);
                config_changed = 1;
            }
            free(config->argv[i]);
        }
        free(config->argv);
        for (int i = config->argc; i < new_config->argc; i++) {
            SAY("Setting %s socket config to '%s'", i == 0 ? "listen" : "forward", new_config->argv[i]);
            config_changed = 1;
        }
        config->argc = new_config->argc;
        config->argv = new_config->argv;
    } else {
        SAY("No listener or forwarder changes");
    }

    if (config->generation && config_changed) {
        SAY("Merged new config");
        config_dump(config);
    }

    int new_syslog_to_stderr = new_config->syslog_to_stderr;
    int syslog_changed = new_config->syslog_to_stderr != config->syslog_to_stderr;

    free(new_config);

    if (config_changed) {
        config->generation++;
        config->epoch_changed = now;
        if (config->config_save && !first_config && !config_save(config, time(NULL))) {
            WARN("Config save failed");
        }
    }
    config->epoch_success = now;

    SAY("Config reload: success");

    SAY("Config reload: generation %ld epoch_attempt %ld epoch_changed %ld epoch_success %ld now %ld",
        (long) config->generation,
        (long) config->epoch_attempt, (long) config->epoch_changed, (long) config->epoch_success, (long) now);

    int syslog_reopen = 0;

    if (control_is(RELAY_STARTING)) {
        if (syslog_changed) {
            if (new_syslog_to_stderr == 0) {
                syslog_reopen = 1;
                config_changed = 1;
            } else {
                /* The converse is sad: the stderr has already been closed. */
                WARN("Changing syslog_to_stderr has no effect (stderr has been closed)");
            }
        }
    } else {
        WARN("Changing syslog_to_stderr has no effect (has effect only on startup)");
    }

    if (config_changed)
        SAY("Config changed: requires restart");
    else
        SAY("Config unchanged: does not require restart");

    if (syslog_reopen) {
        if (new_syslog_to_stderr == 0) {
            SAY("The stderr will stop now, logging will go only to syslog");
            closelog();
            openlog(OUR_NAME, LOG_CONS | LOG_ODELAY | LOG_PID, OUR_FACILITY);
            config->syslog_to_stderr = 0;
        }
    }

    return config_changed;
}


void config_init(int argc, char **argv)
{
    time_t now = time(NULL);

    GLOBAL.config = calloc_or_fatal(sizeof(config_t));
    if (GLOBAL.config == NULL)
        return;

    config_set_defaults(GLOBAL.config);

    closelog();
    openlog(OUR_NAME, LOG_CONS | LOG_ODELAY | LOG_PID | (GLOBAL.config->syslog_to_stderr ? LOG_PERROR : 0),
            OUR_FACILITY);

    if (argc < 2) {
        config_die_args(argc, argv);
    }

    int argi = 0;

    for (argi = 1; argi < argc; argi++) {
        char *p = argv[argi];
        if (*p == '-') {
            p++;
            if (*p == '-')      /* -opt=val or --opt=val */
                p++;
            char *copy = strdup(p);
            int line_num = -1;
            char *opt = NULL, *val = NULL;
            if (config_from_line(GLOBAL.config, argv[argi], copy, &opt, &val, &line_num, "argv")) {
                if (opt) {
                    if (val) {
                        if (STREQ(opt, "config_file"))
                            config_reload(GLOBAL.config, val, now);
                    } else {
                        FATAL("Option %s needs value", argv[argi]);
                    }
                } else {
                    FATAL("Illegal option %s", argv[argi]);
                }
            } else {
                FATAL("Invalid option %s", argv[argi]);
            }
            free(copy);
        } else
            break;
    }

    if (argi < argc) {
        int argn = argc - argi;

        if (argn < 2) {
            config_die_args(argc, argv);
        }

        GLOBAL.config->argv = realloc_or_fatal(GLOBAL.config->argv, sizeof(char *) * argn);
        if (GLOBAL.config->argv == NULL)
            return;
        for (int i = argi; i < argc; i++) {
            GLOBAL.config->argv[i - argi] = strdup(argv[i]);
        }
        GLOBAL.config->argc = argn;
    }

    int config_options_valid = config_valid_options(GLOBAL.config);
    int config_addresses_valid = config_valid_addresses(GLOBAL.config);

    if (!(config_options_valid && config_addresses_valid)) {
        FATAL("Invalid initial configuration");
        return;
    }

    if (GLOBAL.config->config_save) {
        if (!config_save(GLOBAL.config, time(NULL))) {
            WARN("Config save failed");
        }
    }

    closelog();
    openlog(OUR_NAME, LOG_CONS | LOG_ODELAY | LOG_PID | (GLOBAL.config->syslog_to_stderr ? LOG_PERROR : 0),
            OUR_FACILITY);
}


void config_die_args(int argc, char **argv)
{
    (void) argc;
    FATAL("%s [--opt=val ...] [udp@local-host:local-port tcp@remote-host:remote-port ...]", argv[0]);
}
