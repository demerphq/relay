#ifndef _RELAY_H
#define _RELAY_H 

#include "relay_common.h"
#include "util.h"

#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__) || defined(__MACH__)
#include <sys/syslimits.h>
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif


#define RELAY_ATOMIC_INCREMENT(__i, __cnt) __sync_fetch_and_add(&__i, __cnt);
#define RELAY_ATOMIC_DECREMENT(__i, __cnt) __sync_fetch_and_sub(&__i, __cnt);
#define RELAY_ATOMIC_READ(__p) __sync_fetch_and_add(&__p, 0)
#define RELAY_ATOMIC_CMPXCHG(__p, __v1, __v2) __sync_bool_compare_and_swap(&__p, __v1, __v2)
#define RELAY_ATOMIC_CMPXCHG_RETURN(__p, __v1, __v2) __sync_val_compare_and_swap(&__p, __v1, __v2)

#define RELAY_ATOMIC_OR(__i, __flags) __sync_fetch_and_or(&__i, __flags);
#define RELAY_ATOMIC_AND(__i, __flags) __sync_fetch_and_and(&__i, __flags);

#endif
