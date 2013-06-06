#include "relay.h"

static struct sock s;
#ifdef USE_SERVER
#define PREFIX "SERVER"
#else
#define PREFIX "CLIENT"
#endif

static __inline__ unsigned long long rdtsc(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

int main(int ac, char **av) {
    srand(time(NULL) + rdtsc());
    if (ac < 2)
        SAYX(EXIT_FAILURE,"%s remote-ip:remote-port",av[0]);
    socketize(av[1],&s);
    open_socket(&s,DO_NOTHING);
    int do_max = 0;
    int data_size = 128;
    if (ac > 2)
        do_max = atoi(av[2]);

    if (ac > 3)
        data_size = atoi(av[3]);
    if (data_size < 32)
        SAYX(EXIT_FAILURE,"data_size: %d is too small",data_size);

    _D("processing max %d with size %d",do_max,data_size);
    int fd = 0;
#ifdef USE_SERVER
    if (bind(s.socket, (struct sockaddr *) &s.sa.in, s.addrlen) )
        SAYPX("bind");
    if (s.proto == IPPROTO_TCP) {
        if (listen(s.socket,1))
            SAYPX("listen");
        struct sockaddr_un local;
        socklen_t addrlen = sizeof(local);
        fd = accept(s.socket,(struct sockaddr *)&local, &addrlen);
        if (fd == -1)
            SAYPX("accept");
    }
#endif
    fd = fd ? fd : s.socket;
    clock_t start = clock();
    unsigned long i = 0;
    char DATA[data_size];
    unsigned int *magic_place = (unsigned int *) &DATA[sizeof(DATA) - 10];
    DATA[sizeof(DATA) - 1] = '\n';
    #define MAGIC 0xDEADBEEF
    *magic_place = MAGIC;
    while (do_max == 0 || i++ < do_max) {
        #ifdef USE_SERVER
            int rc;
            #ifdef RANDOM_DEATH
            if (s.type != SOCK_DGRAM && (random() % 100000000) == 42)
                SAYX(EXIT_SUCCESS,"death decided by chance");
            #endif
            *magic_place = 0x11111111;
            if ((rc = recv(fd,DATA,sizeof(DATA),MSG_WAITALL)) <= 0) {
                if (rc == 0) // shutdown
                    exit(EXIT_SUCCESS);
                SAYPX("recv: %d",rc);
            }
            if (*magic_place != MAGIC)
                SAYX(EXIT_FAILURE,"magic not found in the received packet %x recv: %d, data_size: %d",*magic_place,rc,data_size);
        #else
            int rc = sendto(fd,DATA,sizeof(DATA),0,(struct sockaddr*) &s.sa.in,s.addrlen);
            if (rc < 0)
                SAYPX("sendto");
        #endif
    }
    unsigned long ms = (clock() - start) * 1000 / CLOCKS_PER_SEC;
    _D(PREFIX " %lu with size %d for %lu ms (expected %f per second)",i,data_size,ms,i/((double)ms/1000));
    return(0);
}
