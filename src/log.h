#ifndef RELAY_LOG_H
#define RELAY_LOG_H

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <syslog.h>
#include <time.h>

#include "control.h"
#include "global.h"
#include "relay_common.h"

#define OUR_FACILITY LOG_LOCAL5

#ifdef DEBUGGING
#define _DEBUG_ARGS ,pthread_self(),
#define DEBUG_FMT  " th:%lu "
#else
#define _DEBUG_ARGS
#define DEBUG_FMT
#endif

#define TS_LEN 40

#define _LOG(type, fmt, arg...) STMT_START {                                                            \
    struct {                                                                                            \
        time_t t;                                                                                       \
        struct tm tm;                                                                                   \
        char str[TS_LEN];                                                                               \
     } ts;                                                                                              \
                                                                                                        \
    (void)time(&ts.t);                                                                                  \
    (void)localtime_r(&ts.t, &ts.tm);                                                                   \
    strftime(ts.str, TS_LEN, "%Y-%m-%d %H:%M:%S", &ts.tm);                                              \
    char logbuf[1024];                                                                                  \
                                                                                                        \
    if (   (LOG_ ## type == LOG_CRIT || LOG_ ## type == LOG_WARNING)                                    \
        && (!control_is(RELAY_RUNNING) || GLOBAL.config == NULL || !GLOBAL.config->syslog_to_stderr))   \
    {                                                                                                   \
        /* dump log record together with timestamp */                                                   \
        fprintf(stderr,                                                                                 \
                "[%4.4s %s" DEBUG_FMT "] " fmt " [%s:%d] %s()\n",                                       \
                "" #type, ts.str _DEBUG_ARGS, ## arg, __FILE__, __LINE__, __func__);                    \
    }                                                                                                   \
                                                                                                        \
    /* do not dump timestamp to syslog, it prevent syslog from deduping repeated messages */            \
    snprintf(logbuf, sizeof(logbuf),                                                                    \
            "[%4.4s" DEBUG_FMT "] " fmt " [%s:%d] %s()\n",                                              \
            "" #type _DEBUG_ARGS, ## arg, __FILE__, __LINE__, __func__);                                \
                                                                                                        \
    syslog((OUR_FACILITY | LOG_ ## type), "%s", logbuf);                                                \
} STMT_END

#define WARN(fmt, arg...) _LOG(WARNING, fmt, ## arg)
#define SAY(fmt, arg...) _LOG(INFO, fmt, ## arg)

#define FATAL(fmt, arg...) STMT_START {	\
    _LOG(CRIT, fmt, ## arg);	\
    control_exit(EXIT_FAILURE);	\
} STMT_END

#define FATAL_ERRNO(fmt, arg...) STMT_START {	\
    _LOG(CRIT, fmt " { %s }", ## arg, errno ? strerror(errno) : "undefined error");	\
    control_exit(EXIT_FAILURE);	\
} STMT_END

#define WARN_ERRNO(fmt, arg...) \
    WARN(fmt " { %s }", ## arg, errno ? strerror(errno) : "undefined error");

#endif				/* #ifndef RELAY_LOG_H */
