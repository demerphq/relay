#ifndef _RELAY_H
#define _RELAY_H 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <stdio.h>
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
#define MAX_CHUNK_SIZE 0xFFFF
#define MAX_QUEUE_SIZE 8192
#ifndef MAX_WORKERS
#define MAX_WORKERS 2
#endif
#define MAX_GARBAGE (MAX_QUEUE_SIZE * MAX_WORKERS)
#define INLINE inline
#ifdef USE_SPINLOCK
#define LOCK_T pthread_spinlock_t
#define LOCK(x) pthread_spin_lock(x)
#define UNLOCK(x) pthread_spin_unlock(x)
#define LOCK_INIT(x) pthread_spin_init(x,0)
#define LOCK_DESTROY(x) pthread_spin_destroy(x)
#else
#define LOCK_T pthread_mutex_t
#define LOCK(x) pthread_mutex_lock(x)
#define UNLOCK(x) pthread_mutex_unlock(x)
#define LOCK_INIT(x) pthread_mutex_init(x,NULL)
#define LOCK_DESTROY(x) pthread_mutex_destroy(x)
#endif

struct blob {
    unsigned int size;
    unsigned int pos;
#ifdef BLOB_ARRAY_DATA
    char data[MAX_CHUNK_SIZE];
#else
    char *data;
#endif
    struct blob *next;
    int id;
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
    socklen_t addrlen;
};

struct worker {
    struct queue queue;
    pthread_mutex_t cond_lock;
    pthread_cond_t cond;
    pthread_t tid;
    unsigned long long sent;
    volatile int abort;
    struct sock s_output;
};
#define THROTTLE_INTERVAL 1
#define THROTTLE_DEBUG 0
#define DO_NOTHING 0
#define DO_BIND 1
#define DO_CONNECT 2
#define FORMAT(fmt,arg...) fmt " [%s():%s:%d @ %u]\n",##arg,__func__,__FILE__,__LINE__,(unsigned int) time(NULL)
#define _E(fmt,arg...) fprintf(stderr,FORMAT(fmt,##arg))
#define _D(fmt,arg...) printf(FORMAT(fmt,##arg))
#define _TD(fmt,arg...) t_fprintf(THROTTLE_DEBUG,stdout,FORMAT(fmt,##arg))
#define SAYX(rc,fmt,arg...) do {    \
    _E(fmt,##arg);                  \
    exit(rc);                       \
} while(0)

#define SAYPX(fmt,arg...) SAYX(EXIT_FAILURE,fmt " { %s }",##arg,errno ? strerror(errno) : "undefined error");

#define SEND(s,b) (((s)->type != SOCK_DGRAM) ? send((s)->socket,(b)->data, (b)->pos,MSG_NOSIGNAL) : \
                                             sendto((s)->socket,(b)->data, (b)->pos,MSG_NOSIGNAL,   \
                                                    (struct sockaddr*) &(s)->sa.in,(s)->addrlen))

#define ENQUEUE(b)                                                                      \
do {                                                                                    \
    if (enqueue_blob_for_transmission(b) <= 0)                                          \
        SAYX(EXIT_FAILURE,"unable to find working thread to enqueue the packet to");    \
} while (0)

/* blob.c */
INLINE void *realloc_or_die(void *p, size_t size);
INLINE void *malloc_or_die(size_t size);
INLINE blob_t * b_new(void);
INLINE void b_throw_in_garbage(blob_t *b);
INLINE blob_t * b_find_in_garbage(void);
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
INLINE void worker_wait(struct worker *worker);
void *worker_thread(void *arg);
int enqueue_blob_for_transmission(blob_t *b);
void worker_destroy_static(void);
void worker_init_static(int ac, char **av);

/* util.c */
void socketize(const char *arg,struct sock *s);
char *socket_to_string(struct sock *s);
void open_socket(struct sock *s,int do_bind);

/* throttle.c */
void throttle_init_static(void);
int is_throttled(unsigned char type);
void t_fprintf(unsigned char type, FILE *stream, const char *format, ...);
#endif
