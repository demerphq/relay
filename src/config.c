#include "config.h"
#include "worker.h"
struct config CONFIG;

void trim(char * s) {
    char *p = s;
    int l = strlen(p);

    while(isspace(p[l - 1])) p[--l] = 0;
    while(*p && isspace(*p)) ++p, --l;

    memmove(s, p, l + 1);
}

void config_reload(void) {
    if (!CONFIG.file)
        return;
    FILE *f;
    char *line;
    ssize_t read;
    size_t len = 0;

    f = fopen(CONFIG.file, "r");
    if (f == NULL)
        SAYPX("fopen: %s",CONFIG.file);

    int i;
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
                    SAYPX("bad config line: %s", line);
                *p = '\0';
                p++;
                if (strcmp("fallback_root", line) == 0) {
                    free(CONFIG.fallback_root);
                    CONFIG.fallback_root = strdup(p);
                } else if (strcmp("polling_interval_ms", line) == 0) {
                    CONFIG.polling_interval_ms = atoi(p);
                } else if (strcmp("sleep_after_disaster_ms", line) == 0) {
                    CONFIG.sleep_after_disaster_ms = atoi(p);
                } else {
                    SAYPX("bad config option: %s",line);
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
    _D("loading config file %s", CONFIG.file);
}

void config_init(int argc, char **argv) {
    int i = 0;
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
    config_reload();
    // assign some default settings

    if (!CONFIG.fallback_root)
        CONFIG.fallback_root = strdup(FALLBACK_ROOT);
    if (CONFIG.polling_interval_ms <= 0)
        CONFIG.polling_interval_ms = POLLING_INTERVAL_MS;
    if (CONFIG.sleep_after_disaster_ms <= 0)
        CONFIG.sleep_after_disaster_ms = SLEEP_AFTER_DISASTER_MS;
    _D("fallback_root: %s", CONFIG.fallback_root);
    _D("polling_intraval_ms: %d", CONFIG.polling_interval_ms);
    _D("sleep_after_disaster_ms: %d", CONFIG.polling_interval_ms);

}
void config_destroy(void) {
    int i;
    for (i = 0; i < CONFIG.argc; i++)
        free(CONFIG.argv[i]);
    free(CONFIG.fallback_root);
    free(CONFIG.file);
}
