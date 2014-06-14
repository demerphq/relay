#include "log.h"
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define DATE_BUF_LEN 11
#define NAME_BUF_LEN 1024


void open_logfile() {
    char datebuf[DATE_BUF_LEN];
    char namebuf[NAME_BUF_LEN];
    time_t rawtime;
    struct tm tm_val;

    time(&rawtime);
    (void)gmtime_r(&rawtime,&tm_val);

    if (
        CONFIG.logdir
        &&
        (DATE_BUF_LEN == strftime(datebuf, DATE_BUF_LEN, "%Y%m%d%H", &tm_val))
        &&
        (NAME_BUF_LEN >= snprintf(namebuf, NAME_BUF_LEN, "%s/relay_log_%s.log", CONFIG.logdir, datebuf))
    ) {
        if ( !CONFIG.logfile || strcmp(CONFIG.logfile, namebuf) !=0 ) {
            if ( ( CONFIG.logfh= fopen( namebuf, "a" ) ) ) {
                free(CONFIG.logfile);
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

