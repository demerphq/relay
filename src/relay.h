#ifndef _RELAY_H
#define _RELAY_H 

#include "relay_common.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#define MAX_CHUNK_SIZE 0xFFFF
#define MAX_QUEUE_SIZE 8192
#ifndef MAX_WORKERS
#define MAX_WORKERS 255
#endif
#define MAX_GARBAGE (MAX_QUEUE_SIZE * MAX_WORKERS)

#define LOCK_T pthread_mutex_t
#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define LOCK_INIT(x) pthread_mutex_init(x,NULL)
#define LOCK_DESTROY(x) pthread_mutex_destroy(x)

#define FALLBACK_ROOT "/tmp"

#ifndef SLEEP_AFTER_DISASTER
#define SLEEP_AFTER_DISASTER 1
#endif
#if defined(__APPLE__) || defined(__MACH__)
# ifndef MSG_NOSIGNAL
#   define MSG_NOSIGNAL SO_NOSIGPIPE
# endif
#endif
/* this structure is shared between different threads */
/* the idea here is that we want a data structure which is exactly
 * 4 bytes of length, followed by K bytes of string */
struct __data_blob {
    uint32_t size;
    char data[0];
} __attribute__ ((packed));
typedef struct __data_blob __data_blob_t;

/* this structure is shared between different threads
 * we use this to refcount __blob_t items, and we use
 * the lock to guard refcnt modifications */
struct _refcnt_blob {
    LOCK_T lock;
    uint32_t refcnt;
    __data_blob_t *data;
};
typedef struct _refcnt_blob _refcnt_blob_t;

/* this structure is private to each thread */
struct blob {
    unsigned int pos;
    struct blob *next;
    _refcnt_blob_t *ref;
};
typedef struct blob blob_t;

struct queue {
    blob_t *head;
    blob_t *tail;
    LOCK_T lock;
    unsigned int count;
    unsigned int limit;
};

struct sock {
    union sa {
        struct sockaddr_un un;
        struct sockaddr_in in;
    } sa;
    int socket;
    int proto;
    int type;
    char to_string[PATH_MAX];
    socklen_t addrlen;
};

#define MAX_PATH 256

struct worker {
    struct queue queue;
    pthread_mutex_t cond_lock;
    pthread_cond_t cond;
    pthread_t tid;
    unsigned long long sent;
    volatile int abort;
    volatile int exit;

    struct sock s_output;
    char fallback_path[PATH_MAX];
    char fallback_file[PATH_MAX];
};

#define DO_NOTHING      0
#define DO_BIND         1
#define DO_CONNECT      2
#define DO_NOT_EXIT     4
#define DO_SET_TIMEOUT  8


#define _D(fmt,arg...) printf(FORMAT(fmt,##arg))
#define _TD(fmt,arg...) t_fprintf(THROTTLE_DEBUG,stdout,FORMAT(fmt,##arg))
#define _TE(fmt,arg...) t_fprintf(THROTTLE_ERROR,stdout,FORMAT(fmt,##arg))

#define SAYPX(fmt,arg...) SAYX(EXIT_FAILURE,fmt " { %s }",##arg,errno ? strerror(errno) : "undefined error");
#define _ENO(fmt,arg...) _E(fmt " { %s }",##arg,errno ? strerror(errno) : "undefined error");
#define SEND(S,B) (((S)->type != SOCK_DGRAM) ? send((S)->socket,(B)->ref->data, (B)->ref->data->size, MSG_NOSIGNAL) : \
                                               sendto((S)->socket,(B)->ref->data, (B)->ref->data->size, MSG_NOSIGNAL,   \
                                                    (struct sockaddr*) &(S)->sa.in,(S)->addrlen))

/* blob.c */
INLINE void *realloc_or_die(void *p, size_t size);
INLINE void *malloc_or_die(size_t size);
INLINE blob_t * b_new(void);
INLINE blob_t * b_clone(blob_t *b);
INLINE void b_set_pos(blob_t *b, unsigned int pos);
INLINE void b_shift(blob_t *b, unsigned int len);
INLINE char * b_data(blob_t *b);
INLINE char * b_data_at_pos(blob_t *b, unsigned int pos);
INLINE void b_prepare(blob_t *b,size_t size);
INLINE void b_destroy(blob_t *b);
void b_init_static(void);
void b_destroy_static(void);

/* worker.c */
struct worker * worker_init(char *arg);
void worker_destroy(struct worker *worker);
INLINE void worker_signal(struct worker *worker);
INLINE void worker_wait(struct worker *worker,int delay);
void *worker_thread(void *arg);
int enqueue_blob_for_transmission(blob_t *b);
void worker_destroy_static(void);
void worker_init_static(int ac, char **av,int destroy);

/* util.c */
void socketize(const char *arg,struct sock *s);
int open_socket(struct sock *s,int do_bind);
#endif
