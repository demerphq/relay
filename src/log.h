#ifndef _LOG_H
#define _LOG_H

#ifdef DEBUGGING
#define _DEBUG_ARGS ,pthread_self(), __func__, __FILE__, __LINE__
#define DEBUG_FMT  " th:%lu %s():%s:%d"
#else
#define _DEBUG_ARGS
#define DEBUG_FMT
#endif

#define FORMAT(type, fmt, arg...) "[$$: %u epoch: %u" DEBUG_FMT " %s ] " fmt "\n", \
    getpid(), (unsigned int) time(NULL) _DEBUG_ARGS, (type), ## arg

#define PERR(type, fmt, arg...) fprintf(stderr, FORMAT(type,fmt, ## arg))

#define WARN(fmt, arg...) PERR("WARN",fmt, ## arg)
#define SAY(fmt, arg...) PERR("INFO",fmt, ## arg)

#define DIE_RC(rc, fmt, arg...) STMT_START {                 \
    PERR(rc == EXIT_FAILURE ? "FATAL" : "QUIT",fmt, ## arg); \
    exit(rc);                                                \
} STMT_END


#define DIE(fmt, arg...) \
    DIE_RC(EXIT_FAILURE, fmt " { %s }", ##arg, errno ? strerror(errno) : "undefined error");
#define WARN_ERRNO(fmt, arg...) \
    WARN(fmt " { %s }", ## arg, errno ? strerror(errno) : "undefined error");

#define CONF_WARN(opt,fmt,a,b)                                               \
    STMT_START {\
        SAY("found different <" opt ">, restart is required for it to take effect. was: <" fmt ">, new: <" fmt ">",a,b); \
    } STMT_END


#endif
