#ifndef _LOG_H
#define _LOG_H

#ifdef DEBUGGING
#define _DEBUG_ARGS __func__, __FILE__, __LINE__,
#define DEBUG_FMT  " %s():%s:%d "
#else
#define _DEBUG_ARGS
#define DEBUG_FMT
#endif

#define FORMAT(fmt, arg...) "[$$: %u epoch: %u th:%lu" DEBUG_FMT "] " fmt "\n", \
    getpid(), (unsigned int) time(NULL), pthread_self() _DEBUG_ARGS, ## arg

#define WARN(fmt, arg...) fprintf(stderr, FORMAT(fmt, ## arg))
#define SAY(fmt, arg...) printf(FORMAT(fmt, ## arg))

#define DIE_RC(rc, fmt, arg...) STMT_START {    \
    WARN(fmt, ## arg);                          \
    exit(rc);                                   \
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
