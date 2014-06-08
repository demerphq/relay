#ifndef _RELAY_COMMON_H
#define _RELAY_COMMON_H

#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "relay_threads.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define MAX_CHUNK_SIZE 0xFFFF

#define DO_NOTHING      0
#define DO_BIND         1
#define DO_CONNECT      2
#define DO_NOT_EXIT     4
#define DO_SET_TIMEOUT  8
#define SEND_TIMEOUT    2

#ifndef NO_INLINE
#define INLINE inline
#else
#define INLINE
#endif

#define FORMAT(fmt, arg...) fmt " [%s():%s:%d @ %u $$: %u : %lu]\n", ##arg, __func__, __FILE__, __LINE__, (unsigned int) time(NULL), getpid(), pthread_self()
#define _E(fmt, arg...) fprintf(stderr, FORMAT(fmt, ##arg))

#define STMT_START do
#define STMT_END while (0)

#define SAYX(rc, fmt, arg...) STMT_START {  \
    _E(fmt, ##arg);                         \
    exit(rc);                               \
} STMT_END

#define _D(fmt, arg...) printf(FORMAT(fmt, ##arg))
#define _TD(fmt, arg...) t_fprintf(THROTTLE_DEBUG, stdout, FORMAT(fmt, ##arg))
#define _TE(fmt, arg...) t_fprintf(THROTTLE_ERROR, stdout, FORMAT(fmt, ##arg))

#define SAYPX(fmt, arg...) SAYX(EXIT_FAILURE, fmt " { %s }", ##arg, errno ? strerror(errno) : "undefined error");
#define _ENO(fmt, arg...) _E(fmt " { %s }", ##arg, errno ? strerror(errno) : "undefined error");

#define RELAY_ATOMIC_INCREMENT(__i, __cnt) __sync_fetch_and_add(&__i, __cnt);
#define RELAY_ATOMIC_DECREMENT(__i, __cnt) __sync_fetch_and_sub(&__i, __cnt);
#define RELAY_ATOMIC_READ(__p) __sync_fetch_and_add(&__p, 0)
#define RELAY_ATOMIC_CMPXCHG(__p, __v1, __v2) __sync_bool_compare_and_swap(&__p, __v1, __v2)
#define RELAY_ATOMIC_CMPXCHG_RETURN(__p, __v1, __v2) __sync_val_compare_and_swap(&__p, __v1, __v2)

#define RELAY_ATOMIC_OR(__i, __flags) __sync_fetch_and_or(&__i, __flags);
#define RELAY_ATOMIC_AND(__i, __flags) __sync_fetch_and_and(&__i, __flags);

#endif
