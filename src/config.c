#include "config.h"

#include "log.h"
#include "string_util.h"
#include "worker.h"

static const char *OUR_NAME = "event-relay";

config_t CONFIG;

void config_destroy(void)
{
    int i;
    for (i = 0; i < CONFIG.argc; i++)
	free(CONFIG.argv[i]);
    free(CONFIG.argv);
    free(CONFIG.graphite_arg);
    free(CONFIG.graphite_root);
    free(CONFIG.spillway_root);
    free(CONFIG.file);
}

void config_set_defaults(config_t * config)
{

    config->graphite_arg = strdup(DEFAULT_GRAPHITE_ARG);
    config->graphite_root = strdup(DEFAULT_GRAPHITE_ROOT);
    config->spillway_root = strdup(DEFAULT_FALLBACK_ROOT);

    config->polling_interval_ms = DEFAULT_POLLING_INTERVAL_MS;
    config->sleep_after_disaster_ms = DEFAULT_SLEEP_AFTER_DISASTER_MS;
    config->tcp_send_timeout = DEFAULT_SEND_TIMEOUT;
    config->server_socket_rcvbuf = DEFAULT_SERVER_SOCKET_RCVBUF;
    config->spill_usec = DEFAULT_SPILL_USEC;
    config->graphite_send_interval_ms = DEFAULT_GRAPHITE_SEND_INTERVAL_MS;
    config->graphite_sleep_poll_interval_ms = DEFAULT_GRAPHITE_SLEEP_POLL_INTERVAL_MS;
    config->syslog_to_stderr = DEFAULT_SYSLOG_TO_STDERR;
}

#define TRY_OPT_BEGIN do
#define TRY_OPT_END   while (0)

#define TRY_NUM_OPT(name,line,p)                                            \
    if ( STREQ(#name, line) ) {                                             \
        int tmp= atoi(p);                                                   \
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
    ssize_t read_bytes;
    size_t len = 0;
    config_t *config = calloc_or_die(sizeof(config_t));
    config_set_defaults(config);

    f = fopen(file, "r");
    if (f == NULL)
	DIE("fopen: %s", file);

    while ((read_bytes = getline(&line, &len, f)) != -1) {
	char *p;
	if ((p = strchr(line, '#')))
	    *p = '\0';

	trim_space(line);

	if (strlen(line) != 0) {
	    if ((p = strchr(line, '='))) {
		if (strlen(p) == 1)
		    DIE("bad config line: %s", line);
		*p = '\0';
		p++;
		TRY_OPT_BEGIN {
		    TRY_STR_OPT(spillway_root, line, p);
		    TRY_STR_OPT(graphite_arg, line, p);
		    TRY_STR_OPT(graphite_root, line, p);
		    TRY_NUM_OPT(syslog_to_stderr, line, p);
		    TRY_NUM_OPT(graphite_send_interval_ms, line, p);
		    TRY_NUM_OPT(graphite_sleep_poll_interval_ms, line, p);
		    TRY_NUM_OPT(polling_interval_ms, line, p);
		    TRY_NUM_OPT(sleep_after_disaster_ms, line, p);
		    TRY_NUM_OPT(tcp_send_timeout, line, p);
		    TRY_NUM_OPT(server_socket_rcvbuf, line, p);
		    TRY_NUM_OPT(spill_usec, line, p);

		    WARN("ignoring bad config option: %s", line);
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
    SAY("loaded config file %s", file);
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
    config_t *new_config = config_from_file(config->file);

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
    IF_STR_OPT_CHANGED(graphite_arg, config, new_config);
    IF_STR_OPT_CHANGED(graphite_root, config, new_config);
    IF_NUM_OPT_CHANGED(syslog_to_stderr, config, new_config);
    IF_NUM_OPT_CHANGED(graphite_send_interval_ms, config, new_config);
    IF_NUM_OPT_CHANGED(graphite_sleep_poll_interval_ms, config, new_config);
    IF_NUM_OPT_CHANGED(polling_interval_ms, config, new_config);
    IF_NUM_OPT_CHANGED(sleep_after_disaster_ms, config, new_config);
    IF_NUM_OPT_CHANGED(tcp_send_timeout, config, new_config);
    IF_NUM_OPT_CHANGED(server_socket_rcvbuf, config, new_config);
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
