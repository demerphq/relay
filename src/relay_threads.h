#ifndef _RELAY_THREADS_H
#define _RELAY_THREADS_H 

#include <pthread.h>
#define LOCK_T pthread_mutex_t
#define LOCK(x) if (x) pthread_mutex_lock(x)
#define UNLOCK(x) if (x) pthread_mutex_unlock(x)
#define LOCK_INIT(x) pthread_mutex_init(x, NULL)
#define LOCK_DESTROY(x) pthread_mutex_destroy(x)

#endif
