#ifndef RELAY_RELAY_COMMON_H
#define RELAY_RELAY_COMMON_H

#include <assert.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "relay_threads.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define MAX_CHUNK_SIZE 0xFFFF

#ifndef NO_INLINE
#define INLINE inline
#else
#define INLINE
#endif

#define RELAY_ATOMIC_INCREMENT(__i, __cnt) __sync_fetch_and_add(&__i, __cnt);
#define RELAY_ATOMIC_DECREMENT(__i, __cnt) __sync_fetch_and_sub(&__i, __cnt);
#define RELAY_ATOMIC_READ(__p) __sync_fetch_and_add(&__p, 0)
#define RELAY_ATOMIC_CMPXCHG(__p, __v1, __v2) __sync_bool_compare_and_swap(&__p, __v1, __v2)
#define RELAY_ATOMIC_CMPXCHG_RETURN(__p, __v1, __v2) __sync_val_compare_and_swap(&__p, __v1, __v2)

#define RELAY_ATOMIC_OR(__i, __flags) __sync_fetch_and_or(&__i, __flags);
#define RELAY_ATOMIC_AND(__i, __flags) __sync_fetch_and_and(&__i, __flags);

#define STMT_START do
#define STMT_END while(0)

/* this defines things like TAILQ_ENTRY() see man TAILQ_ENTRY for details */
#include <sys/queue.h>
/* fixups for stuff that might be missing from sys/queue.h */
#ifdef __linux__
#define TAILQ_EMPTY(head)       ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head)       ((head)->tqh_first)
#ifndef TAILQ_END
#define        TAILQ_END(head)                        NULL
#endif
#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)      \
        for ((var) = TAILQ_FIRST(head);                 \
            (var) != TAILQ_END(head) &&                 \
            ((tvar) = TAILQ_NEXT(var, field), 1);       \
            (var) = (tvar))
#endif
#endif

#define EXIT_FLAG 1

#endif				/* #ifndef RELAY_RELAY_COMMON_H */
