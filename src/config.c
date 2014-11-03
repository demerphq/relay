#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include "global.h"
#include "log.h"
#include "socket_worker.h"
#include "string_util.h"

static const char *OUR_NAME = "event-relay";

void config_destroy(void)
{
    int i;
    for (i = 0; i < GLOBAL.config->argc; i++)
	free(GLOBAL.config->argv[i]);
    free(GLOBAL.config->argv);
    free(GLOBAL.config->graphite.addr);
    free(GLOBAL.config->graphite.target);
    free(GLOBAL.config->spill_root);
    free(GLOBAL.config->file);
    free(GLOBAL.config);
}

void config_set_defaults(config_t * config)
{
    if (config == NULL)
	return;

    config->syslog_to_stderr = DEFAULT_SYSLOG_TO_STDERR;
    config->tcp_send_timeout_millisec = DEFAULT_TCP_SEND_TIMEOUT_MILLISEC;
    config->polling_interval_millisec = DEFAULT_POLLING_INTERVAL_MILLISEC;
    config->sleep_after_disaster_millisec = DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC;
    config->server_socket_rcvbuf_bytes = DEFAULT_SERVER_SOCKET_RCVBUF_BYTES;

    config->spill_millisec = DEFAULT_SPILL_MILLISEC;
    config->spill_root = strdup(DEFAULT_SPILL_ROOT);

    config->graphite.addr = strdup(DEFAULT_GRAPHITE_ADDR);
    config->graphite.target = strdup(DEFAULT_GRAPHITE_TARGET);
    config->graphite.send_interval_millisec = DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC;
    config->graphite.sleep_poll_interval_millisec = DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC;
}

void config_dump(config_t * config)
{
    if (config == NULL)
	return;
    SAY("config->syslog_to_stderr = %d", config->syslog_to_stderr);
    SAY("config->tcp_send_timeout_millisec = %d", config->tcp_send_timeout_millisec);
    SAY("config->polling_interval_millisec = %d", config->polling_interval_millisec);
    SAY("config->sleep_after_disaster_millisec = %d", config->sleep_after_disaster_millisec);
    SAY("config->server_socket_rcvbuf_bytes = %d", config->server_socket_rcvbuf_bytes);


    SAY("config->spill_root = %s", config->spill_root);
    SAY("config->spill_millisec = %d", config->spill_millisec);

    SAY("config->graphite.addr = %s", config->graphite.addr);
    SAY("config->graphite.target = %s", config->graphite.target);
    SAY("config->graphite.send_interval_millisec = %d", config->graphite.send_interval_millisec);
    SAY("config->graphite.sleep_poll_interval_millisec = %d", config->graphite.sleep_poll_interval_millisec);
    if (config->argc > 0)
	SAY("listener address = %s", config->argv[0]);
    for (int i = 1; i < config->argc; i++)
	SAY("forward address = %s", config->argv[i]);
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
    /* XXX maybe stricter: can the words start with a digit? */
    for (p = path; *p && (isalnum(*p) || *p == '_'); p++) {
	while (isalnum(*p) || *p == '_')
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

static int is_valid_directory(const char *path)
{
    if (!is_non_empty_string(path))
	return 0;
    struct stat st;
    /* Yes, there's a race condition here. */
    return (stat(path, &st) == 0 || S_ISDIR(st.st_mode)) ? 1 : 0;
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
    /* Pretty arbitrary choices but let's require alignment by 4096,
     * and at least one megabyte. */
    return ((size & 4095) == 0) && (size >= 1 << 20);
}

#define CONFIG_VALID_STR(config, t, v, invalid)		\
    do { if (!t(config->v)) { SAY("%s value '%s' invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_SOCKETIZE(config, p, d, r, v, invalid)		\
    do { if (!is_valid_socketize(config->v, p, d, r " (config check)")) { SAY("%s value '%s' invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_NUM(config, t, v, invalid)		\
    do { if (!t(config->v)) { SAY("%s value %d invalid", #v, config->v); invalid++; } } while (0)

static int config_valid(config_t * config)
{
    int invalid = 0;

    CONFIG_VALID_NUM(config, is_valid_millisec, tcp_send_timeout_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, polling_interval_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, sleep_after_disaster_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_buffer_size, server_socket_rcvbuf_bytes, invalid);

    CONFIG_VALID_STR(config, is_valid_directory, spill_root, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, spill_millisec, invalid);

    CONFIG_VALID_SOCKETIZE(config, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "graphite worker", graphite.addr, invalid);
    CONFIG_VALID_STR(config, is_valid_graphite_target, graphite.target, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, graphite.send_interval_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, graphite.sleep_poll_interval_millisec, invalid);

    if (config->argc < 1) {
	SAY("Missing listener address");
	invalid++;
    } else {
	CONFIG_VALID_SOCKETIZE(config, IPPROTO_UDP, RELAY_CONN_IS_INBOUND, "listener", argv[0], invalid);
    }
    if (config->argc < 2) {
	SAY("Missing forward addresses");
	invalid++;
    } else {
	for (int i = 1; i < config->argc; i++) {
	    CONFIG_VALID_SOCKETIZE(config, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "forward", argv[i], invalid);
	}
    }

    return invalid == 0;
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

static config_t *config_from_file(char *file)
{
    FILE *f;
    char *line = NULL;
    size_t len = 0;
    int line_num = 0;
    config_t *config = calloc_or_fatal(sizeof(config_t));

    if (config == NULL)
	return NULL;

    config_set_defaults(config);

    if (file == NULL) {
	SAY("Config file unknown");
	return NULL;
    }
    SAY("Loading config file %s", file);
    f = fopen(file, "r");
    if (f == NULL) {
	WARN_ERRNO("Failed to open: %s", file);
	return NULL;
    }

    while (getline(&line, &len, f) != -1) {
	char *p;

	line_num++;

	/* End-of-line comment. */
	if ((p = strchr(line, '#')))
	    *p = '\0';

	trim_space(line);

	if (*line) {
	    if ((p = strchr(line, '='))) {
		if (strlen(p) == 1) {
		    SAY("Error in config file %s:%d: %s", file, line_num, line);
		    return NULL;
		}
		*p = '\0';
		p++;
		TRY_OPT_BEGIN {
		    TRY_NUM_OPT(syslog_to_stderr, line, p);

		    TRY_NUM_OPT(tcp_send_timeout_millisec, line, p);
		    TRY_NUM_OPT(polling_interval_millisec, line, p);
		    TRY_NUM_OPT(sleep_after_disaster_millisec, line, p);
		    TRY_NUM_OPT(server_socket_rcvbuf_bytes, line, p);

		    TRY_STR_OPT(spill_root, line, p);
		    TRY_NUM_OPT(spill_millisec, line, p);

		    TRY_STR_OPT(graphite.addr, line, p);
		    TRY_STR_OPT(graphite.target, line, p);
		    TRY_NUM_OPT(graphite.send_interval_millisec, line, p);
		    TRY_NUM_OPT(graphite.sleep_poll_interval_millisec, line, p);

		    SAY("Error in config file %s:%d: bad config option: %s", file, line_num, line);
		    return NULL;
		}
		TRY_OPT_END;

	    } else {
		config->argv = realloc_or_fatal(config->argv, sizeof(line) * (config->argc + 1));
		if (config->argv == NULL)
		    return NULL;

		config->argv[config->argc] = strdup(line);
		config->argc++;
	    }
	}
    }
    fclose(f);
    if (line)
	free(line);
    SAY("Loaded config file %s", file);

    if (!config_valid(config)) {
	config_dump(config);
	SAY("Invalid configuration");
	return NULL;
    }

    return config;
}

static int config_to_buffer(const config_t * config, fixed_buffer_t * buf)
{
#define CONFIG_NUM_VCATF(name) \
    if (!fixed_buffer_vcatf(buf, #name " = %d\n", config->name)) return 0;
#define CONFIG_STR_VCATF(name) \
    if (!fixed_buffer_vcatf(buf, #name " = %s\n", config->name)) return 0;

    CONFIG_NUM_VCATF(syslog_to_stderr);
    CONFIG_NUM_VCATF(tcp_send_timeout_millisec);
    CONFIG_NUM_VCATF(polling_interval_millisec);
    CONFIG_NUM_VCATF(sleep_after_disaster_millisec);
    CONFIG_NUM_VCATF(server_socket_rcvbuf_bytes);

    CONFIG_STR_VCATF(spill_root);
    CONFIG_NUM_VCATF(spill_millisec);

    CONFIG_STR_VCATF(graphite.addr);
    CONFIG_STR_VCATF(graphite.target);
    CONFIG_NUM_VCATF(graphite.send_interval_millisec);
    CONFIG_NUM_VCATF(graphite.sleep_poll_interval_millisec);

    for (int i = 0; i < config->argc; i++) {
	if (!fixed_buffer_vcatf(buf, "%s\n", config->argv[i]))
	    return 0;
    }
    return 1;
}

static int config_to_file(const config_t * config, int fd)
{
    fixed_buffer_t *buf = fixed_buffer_create(4096);
    int success = 0;

    if (buf) {
	if (config_to_buffer(config, buf)) {
	    ssize_t wrote;
	    if ((wrote = write(fd, buf->data, buf->used)) == buf->used) {
		success = 1;
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
    if (config->file == NULL) {
	WARN("Failed to save config with NULL name");
	return 0;
    }

    /* We will do mkstemp() + rename(), but first we need a temp file
     * name template, and we need it in in the same directory as the
     * configuration file. */
    char temp[PATH_MAX];
    char *p = config->file;
    char *q = temp;
    char *qe = temp + sizeof(temp);
    /* Safer than strcpy or strncpy */
    while (*p && q < qe)
	*q++ = *p++;
    if (q < qe) {
	*q = 0;
    } else {
	WARN("Failed to copy config filename %s", config->file);
	return 0;
    }

    /* dirname() is rather unportable in its behavior,
     * unfortunately. */
    char *slash = NULL;
    for (q = temp; *q; q++)
	if (*q == '/')
	    slash = q;

    const char base[] = "event-relay.conf.XXXXXX";

    int wrote;
    int room;
    if (slash) {
	*slash = 0;
	int len = slash - temp;
	room = sizeof(temp) - len - 1;
	wrote = snprintf(temp + len, room, "/%s", base);
    } else {
	room = sizeof(temp) - 2;
	wrote = snprintf(temp, room, "./%s", base);
    }
    if (wrote < 0 || wrote >= room) {
	WARN_ERRNO("Failed making filename %s", temp);
	return 0;
    }

    int fd = mkstemp(temp);
    if (fd == -1) {
	WARN_ERRNO("Failed to mkstemp %s", temp);
	return 0;
    }

    if (!config_to_file(config, fd)) {
	WARN_ERRNO("Failed to save config to %s", temp);
	return 0;
    }

    if (close(fd) == -1) {
	WARN_ERRNO("Failed to close save config as %s", temp);
	return 0;
    }

    char save[PATH_MAX];
    wrote = snprintf(save, PATH_MAX, "%s.save.%ld", config->file, now);
    if (wrote < 0 || wrote >= PATH_MAX) {
	WARN("Failed to write %s as %s.save (wrote %d bytes)", save, save, wrote);
	return 0;
    }

    if (rename(temp, save) != 0) {
	WARN_ERRNO("Failed to rename %s as %s", temp, save);
	return 0;
    }

    SAY("Saved config as %s", save);
    return 1;
}

#define IF_NUM_OPT_CHANGED(name,config,new_config)          \
  do { \
    if ( config->name != new_config->name ) {               \
        SAY("Changed '" #name "' from '%d' to '%d'",        \
                config->name, new_config->name);            \
        config->name = new_config->name;                    \
        config_changed = 1;                                 \
    } \
  } while(0)

#define IF_STR_OPT_CHANGED(name,config,new_config)          \
  do { \
    if ( STRNE(config->name, new_config->name) )       {    \
        SAY("Changed '" #name "' from '%s' to '%s'",        \
                config->name, new_config->name);            \
        free(config->name);                                 \
        config->name = new_config->name;                    \
        config_changed = 1;                                 \
    } \
  } while(0)

int config_reload(config_t * config)
{
    time_t now = time(NULL);
    int config_changed = 0;

    config->epoch_attempt = now;

    SAY("Config reload start: generation %ld epoch_attempt %ld epoch_changed %ld epoch_success %ld now %ld",
	(long) config->generation,
	(long) config->epoch_attempt, (long) config->epoch_changed, (long) config->epoch_success, (long) now);

    if (config->generation == 0) {
	SAY("Loading config file %s", config->file);
	config_changed = 1;
    } else
	SAY("Reloading config file %s", config->file);

    config_t *new_config = config_from_file(config->file);

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

    if (config->generation == 0) {
	SAY("Loaded config file %s", config->file);
	SAY("Initial config");
    } else {
	SAY("Reloaded config file %s", config->file);
	SAY("New unmerged config");
    }

    config_dump(new_config);

    if (!config_valid(new_config)) {
	if (config->generation == 0)
	    FATAL("Invalid initial configuration");
	else
	    WARN("Invalid new configuration, ignoring it");
	return 0;
    }

    if (config->generation)
	SAY("Merging new configuration with old");

    if (config->syslog_to_stderr != new_config->syslog_to_stderr) {
	closelog();
	openlog(OUR_NAME,
		LOG_CONS | LOG_ODELAY | LOG_PID | (new_config->syslog_to_stderr ? LOG_PERROR : 0), OUR_FACILITY);
	if (config->generation == 0)
	    SAY("Setting 'syslog_to_stderr' to '%d'", new_config->syslog_to_stderr);
	else
	    SAY("Changing 'syslog_to_stderr' from '%d' to '%d'", config->syslog_to_stderr,
		new_config->syslog_to_stderr);
	config->syslog_to_stderr = new_config->syslog_to_stderr;
	config_changed = 1;
    }

    IF_NUM_OPT_CHANGED(tcp_send_timeout_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(polling_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(sleep_after_disaster_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(server_socket_rcvbuf_bytes, config, new_config);

    IF_STR_OPT_CHANGED(spill_root, config, new_config);
    IF_NUM_OPT_CHANGED(spill_millisec, config, new_config);

    IF_STR_OPT_CHANGED(graphite.addr, config, new_config);
    IF_STR_OPT_CHANGED(graphite.target, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.send_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.sleep_poll_interval_millisec, config, new_config);

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

    if (config->generation && config_changed) {
	SAY("Merged new config");
	config_dump(config);
    }

    free(new_config);

    if (config_changed) {
	config->generation++;
	config->epoch_changed = now;
	if (!config_save(config, now)) {
	    WARN("Config save failed");
	}
    }
    config->epoch_success = now;

    SAY("Config reload: success");

    SAY("Config reload: generation %ld epoch_attempt %ld epoch_changed %ld epoch_success %ld now %ld",
	(long) config->generation,
	(long) config->epoch_attempt, (long) config->epoch_changed, (long) config->epoch_success, (long) now);

    if (config_changed)
	SAY("Config changed: requires restart");
    else
	SAY("Config unchanged: does not require restart");

    return config_changed;
}


void config_init(int argc, char **argv)
{
    GLOBAL.config = calloc_or_fatal(sizeof(config_t));
    if (GLOBAL.config == NULL)
	return;

    config_set_defaults(GLOBAL.config);
    openlog(OUR_NAME, LOG_CONS | LOG_ODELAY | LOG_PID | (GLOBAL.config->syslog_to_stderr ? LOG_PERROR : 0),
	    OUR_FACILITY);

    if (argc < 2) {
	config_die_args(argc, argv);
    } else if (argc == 2) {
	GLOBAL.config->file = strdup(argv[1]);
	config_reload(GLOBAL.config);
    } else {
	GLOBAL.config->argv = realloc_or_fatal(GLOBAL.config->argv, sizeof(char *) * (argc));
	if (GLOBAL.config->argv == NULL)
	    return;
	int i;
	for (i = 0; i < argc - 1; i++) {
	    GLOBAL.config->argv[i] = strdup(argv[i + 1]);
	}
	GLOBAL.config->argc = i;
    }
}


void config_die_args(int argc, char **argv)
{
    (void) argc;
    FATAL("%s local-host:local-port tcp@remote-host:remote-port ...\n" "or config file\n", argv[0]);
}
