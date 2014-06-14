#include "log.h"

void open_logfile() {
    char datebuf[11];
    char namebuf[1024];
    uint32_t len= 1024;
    struct tm time;
    if (
        CONFIG.logdir
        &&
        (0  == gettimeofday(&time,NULL))
        &&
        (11 == strftime(datebuf, 11, "%Y%m%d%H", time))
        &&
        (1024 >= snprintf(namebuf, 1024, "%s/relay_log_%s.log", CONFIG.logfile, datebuf))
    ) {
        if ( !CONFIG.logfile || strcmp(CONFIG.logfile, namebuf) !=0 ) {
            if ( CONFIG.logfh= fopen( namebuf, "a" ) ) {
                free(CONFIG.logfile)
                CONFIG.logfile= strdup(namebuf);
                return;
            }
            /* fallthrough */
        } else {
            return;
        }
    }

    CONFIG.logfh = stderr;
    free(CONFIG.logfile);
    CONFIG.logfile= NULL;
}

