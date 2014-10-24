#include "config.h"

#include "log.h"
#include "string_util.h"
#include "worker.h"

static const char *OUR_NAME = "event-relay";

/* Global, boo, hiss. */
config_t CONFIG;

void config_destroy(void)
{
    int i;
    for (i = 0; i < CONFIG.argc; i++)
	free(CONFIG.argv[i]);
    free(CONFIG.argv);
    free(CONFIG.graphite.addr);
    free(CONFIG.graphite.target);
    free(CONFIG.spillway_root);
    free(CONFIG.file);
}

void config_set_defaults(config_t * config)
{
    config->spillway_root = strdup(DEFAULT_SPILLWAY_ROOT);

    config->polling_interval_millisec = DEFAULT_POLLING_INTERVAL_MILLISEC;
    config->sleep_after_disaster_millisec = DEFAULT_SLEEP_AFTER_DISASTER_MILLISEC;
    config->tcp_send_timeout_sec = DEFAULT_TCP_SEND_TIMEOUT_SEC;
    config->server_socket_rcvbuf_bytes = DEFAULT_SERVER_SOCKET_RCVBUF_BYTES;
    config->spill_usec = DEFAULT_SPILL_USEC;

    config->graphite.addr = strdup(DEFAULT_GRAPHITE_ADDR);
    config->graphite.target = strdup(DEFAULT_GRAPHITE_TARGET);
    config->graphite.send_interval_millisec = DEFAULT_GRAPHITE_SEND_INTERVAL_MILLISEC;
    config->graphite.sleep_poll_interval_millisec = DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MILLISEC;

    config->syslog_to_stderr = DEFAULT_SYSLOG_TO_STDERR;
}

void config_dump(config_t * config)
{
    SAY("config->spillway_root = %s", config->spillway_root);
    SAY("config->polling_interval_millisec = %d", config->polling_interval_millisec);
    SAY("config->sleep_after_disaster_millisec = %d", config->sleep_after_disaster_millisec);
    SAY("config->tcp_send_timeout_sec = %d", config->tcp_send_timeout_sec);
    SAY("config->server_socket_rcvbuf_bytes = %d", config->server_socket_rcvbuf_bytes);
    SAY("config->graphite.addr = %s", config->graphite.addr);
    SAY("config->graphite.target = %s", config->graphite.target);
    SAY("config->graphite.send_interval_millisec = %d", config->graphite.send_interval_millisec);
    SAY("config->graphite.sleep_poll_interval_millisec = %d", config->graphite.sleep_poll_interval_millisec);
    SAY("config->syslog_to_stderr = %d", config->syslog_to_stderr);
}

static int is_non_empty_string(const char *s)
{
    return s && *s ? 1 : 0;
}

/* Accepts only ASCII paths: one or more 'words',
 * separated by single dots. */
static int is_valid_graphite_path(const char *path)
{
    if (!is_non_empty_string(path))
	return 0;
    const char *p;
    /* XXX maybe stricter: can the words start with a digit? */
    for (p = path; isalnum(*p) || *p == '_'; p++) {
	while (isalnum(*p) || *p == '_')
	    p++;
	if (*p == '.')
	    continue;
	else if (*p)
	    return 0;
    }
    return *p == 0;
}

static int is_valid_socketize(const char *arg, int default_proto, int connection_direction, const char* role)
{
    if (!is_non_empty_string(arg))
	return 0;
    /* NOTE: the result socketization is "lost" (beyond the success/failure)
     * and redone later when the listener and workers are started.  This may
     * be considered wasteful, but would get tricky on e.g. config reloads. */
    sock_t s;
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

static int is_valid_millisec(uint32_t millisec)
{
    /* The upper limit is because of use of usleep():
     * 1000000 (1 sec) is promised by standards, but no more. */
    return millisec > 0 && millisec <= 1000000;
}

static int is_valid_microsec(uint32_t microsec)
{
    /* The upper limit is because of use of usleep():
     * 1000000 (1 sec) is promised by standards, but no more. */
    return microsec > 0 && microsec <= 1000000;
}

static int is_valid_sec(uint32_t sec)
{
    /* The upper limit is pretty arbitrary, but the basic idea is to
     * protect against too high values which indicate either mixing
     * with milliseconds, or overflows/wraparounds. */
    return sec > 0 && sec <= 60;
}

static int is_valid_buffer_size(uint32_t size)
{
    /* Pretty arbitrary choices but let's require alignment by 4096,
     * and at least one megabyte. */
    return ((size & 4095) == 0) && (size >= 1 << 20);
}

#define CONFIG_VALID_STR(config, t, v, invalid)		\
    do { if (!t(config->v)) { SAY("%s value %s invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_SOCKETIZE(config, p, d, r, v, invalid)		\
    do { if (!is_valid_socketize(config->v, p, d, r " (config check)")) { SAY("%s value %s invalid", #v, config->v); invalid++; } } while (0)

#define CONFIG_VALID_NUM(config, t, v, invalid)		\
    do { if (!t(config->v)) { SAY("%s value %d invalid", #v, config->v); invalid++; } } while (0)

static int config_valid(config_t * config)
{
    int invalid = 0;

    CONFIG_VALID_STR(config, is_valid_directory, spillway_root, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, polling_interval_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_millisec, sleep_after_disaster_millisec, invalid);
    CONFIG_VALID_NUM(config, is_valid_sec, tcp_send_timeout_sec, invalid);
    CONFIG_VALID_NUM(config, is_valid_microsec, spill_usec, invalid);
    CONFIG_VALID_NUM(config, is_valid_buffer_size, server_socket_rcvbuf_bytes, invalid);

    CONFIG_VALID_SOCKETIZE(config, IPPROTO_TCP, RELAY_CONN_IS_OUTBOUND, "graphite worker", graphite.addr, invalid);
    CONFIG_VALID_STR(config, is_valid_graphite_path, graphite.target, invalid);
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
        int tmp = atoi(p);                                                  \
        if (tmp > 0) {                                                      \
            config->name = tmp;                                             \
        } else {                                                            \
            SAY("Ignoring " #name " setting of %d which is too low", tmp);  \
        }                                                                   \
        break;                                                              \
    }

#define TRY_STR_OPT(name,line,p)                                            \
    if ( STREQ(#name, line) ) {                                             \
        free(config->name);                                                 \
        config->name = strdup(p);                                           \
        break;                                                              \
    }

config_t *config_from_file(char *file)
{
    FILE *f;
    char *line = NULL;
    size_t len = 0;
    int line_num = 0;
    config_t *config = calloc_or_die(sizeof(config_t));

    config_set_defaults(config);

    SAY("Loading config file %s", file);
    f = fopen(file, "r");
    if (f == NULL)
	DIE("fopen: %s", file);

    while (getline(&line, &len, f) != -1) {
	char *p;

	line_num++;

	/* End-of-line comment. */
	if ((p = strchr(line, '#')))
	    *p = '\0';

	trim_space(line);

	if (strlen(line) != 0) {
	    if ((p = strchr(line, '='))) {
		if (strlen(p) == 1)
		    DIE("config file %s:%d: %s", file, line_num, line);
		*p = '\0';
		p++;
		TRY_OPT_BEGIN {
		    TRY_STR_OPT(spillway_root, line, p);
		    TRY_STR_OPT(graphite.addr, line, p);
		    TRY_STR_OPT(graphite.target, line, p);
		    TRY_NUM_OPT(syslog_to_stderr, line, p);
		    TRY_NUM_OPT(graphite.send_interval_millisec, line, p);
		    TRY_NUM_OPT(graphite.sleep_poll_interval_millisec, line, p);
		    TRY_NUM_OPT(polling_interval_millisec, line, p);
		    TRY_NUM_OPT(sleep_after_disaster_millisec, line, p);
		    TRY_NUM_OPT(tcp_send_timeout_sec, line, p);
		    TRY_NUM_OPT(server_socket_rcvbuf_bytes, line, p);
		    TRY_NUM_OPT(spill_usec, line, p);

		    DIE("config file %s:%d: bad config option: %s", file, line_num, line);
		}
		TRY_OPT_END;

	    } else {
		config->argv = realloc_or_die(config->argv, sizeof(line) * (config->argc + 1));
		config->argv[config->argc] = strdup(line);
		config->argc++;
	    }
	}
    }
    fclose(f);
    if (line)
	free(line);
    SAY("Loaded config file %s", file);

    config_dump(config);

    if (!config_valid(config))
	DIE("Invalid configuration");

    return config;
}

#define IF_NUM_OPT_CHANGED(name,config,new_config)          \
  do { \
    if ( config->name != new_config->name ) {               \
        SAY("changed '" #name "' from '%d' to '%d'",        \
                config->name, new_config->name);            \
        config->name= new_config->name;                     \
        requires_restart= 1;                                \
    } \
  } while(0)

#define IF_STR_OPT_CHANGED(name,config,new_config)          \
  do { \
    if ( STRNE(config->name, new_config->name) )       {    \
        SAY("changed '" #name "' from '%s' to '%s'",        \
                config->name, new_config->name);            \
        free(config->name);                                 \
        config->name= new_config->name;                     \
        requires_restart= 1;                                \
    } \
  } while(0)

int config_reload(config_t * config)
{
    int i = 0;
    int requires_restart = 0;
    config_t *new_config;

    SAY("Reloading config file %s", config->file);

    new_config = config_from_file(config->file);

    SAY("Reloaded config file %s", config->file);

    SAY("New unmerged configuration");
    config_dump(new_config);
    if (!config_valid(new_config)) {
	SAY("Invalid new configuration, ignoring it");
	return 0;
    }
    SAY("Merging new configuration with old");

    if (new_config->argc < 2) {
	DIE("No server specified?");
    }
    if (config->syslog_to_stderr != new_config->syslog_to_stderr) {
	closelog();
	openlog(OUR_NAME,
		LOG_CONS | LOG_ODELAY | LOG_PID | (new_config->syslog_to_stderr ? LOG_PERROR : 0), OUR_FACILITY);
	SAY("changed 'syslog_to_stderr' from '%d' to '%d'", config->syslog_to_stderr, new_config->syslog_to_stderr);
	config->syslog_to_stderr = new_config->syslog_to_stderr;
	requires_restart = 1;
    }

    IF_STR_OPT_CHANGED(spillway_root, config, new_config);
    IF_STR_OPT_CHANGED(graphite.addr, config, new_config);
    IF_STR_OPT_CHANGED(graphite.target, config, new_config);
    IF_NUM_OPT_CHANGED(syslog_to_stderr, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.send_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(graphite.sleep_poll_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(polling_interval_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(sleep_after_disaster_millisec, config, new_config);
    IF_NUM_OPT_CHANGED(tcp_send_timeout_sec, config, new_config);
    IF_NUM_OPT_CHANGED(server_socket_rcvbuf_bytes, config, new_config);
    IF_NUM_OPT_CHANGED(spill_usec, config, new_config);

    for (i = 0; i < config->argc; i++) {
	if (i < new_config->argc) {
	    if (STRNE(config->argv[i], new_config->argv[i])) {
		SAY("Changing %s socket config from '%s' to '%s'",
		    i == 0 ? "listen" : "forward", config->argv[i], new_config->argv[i]);
		requires_restart = 1;
	    }
	} else {
	    SAY("Stopping forward socket to '%s'", config->argv[i]);
	    requires_restart = 1;
	}
	free(config->argv[i]);
    }
    free(config->argv);
    for (i = config->argc; i < new_config->argc; i++) {
	SAY("Setting new %s socket config to '%s'", i == 0 ? "listen" : "forward", new_config->argv[i]);
	requires_restart = 1;
    }
    config->argc = new_config->argc;
    config->argv = new_config->argv;
    free(new_config);

    SAY("Merged new configuration");
    config_dump(config);

    if (requires_restart)
	SAY("Configuration changed: requires restart");
    else
	SAY("Configuration unchanged: does not require restart");

    return requires_restart;
}


void config_init(int argc, char **argv)
{
    int i = 0;
    memset(&CONFIG, 0, sizeof(CONFIG));
    config_set_defaults(&CONFIG);
    openlog(OUR_NAME, LOG_CONS | LOG_ODELAY | LOG_PID | (CONFIG.syslog_to_stderr ? LOG_PERROR : 0), OUR_FACILITY);

    if (argc < 2) {
	config_die_args(argc, argv);
    } else if (argc == 2) {
	CONFIG.file = strdup(argv[1]);
	config_reload(&CONFIG);
    } else {
	CONFIG.argv = realloc_or_die(CONFIG.argv, sizeof(char *) * (argc));
	for (i = 0; i < argc - 1; i++) {
	    CONFIG.argv[i] = strdup(argv[i + 1]);
	}
	CONFIG.argc = i;
    }
}


void config_die_args(int argc, char **argv)
{
    (void) argc;
    /* XXX: fix me! */
    /* XXX: how?!!! */
    DIE_RC(EXIT_FAILURE,
	   "%s local-host:local-port tcp@remote-host:remote-port ...\n"
	   "or file with socket description like:\n"
	   "\tlocal-host:local-port\n" "\ttcp@remote-host:remote-port ...\n", argv[0]);
}
