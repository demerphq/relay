#include "relay.h"
#include "relay_threads.h"

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

#define MAGIC 0xDEADBEEF
#define DATA_SIZE 128
int main(int ac, char **av) {
    int do_max = 0;
    int data_size = DATA_SIZE;
    int fd = 0;
    unsigned long i = 0;
    unsigned long ms;
    clock_t start;
    char DATA[DATA_SIZE];
    unsigned int *magic_place;

    srand(time(NULL) + rdtsc());
    if (ac < 2)
        SAYX(EXIT_FAILURE,"%s remote-ip:remote-port",av[0]);
    socketize(av[1],&s);
    open_socket(&s,DO_NOTHING);
    if (ac > 2)
        do_max = atoi(av[2]);

    if (ac > 3)
        data_size = atoi(av[3]);
    if (data_size < 32)
        SAYX(EXIT_FAILURE,"data_size: %d is too small",data_size);

    _D("processing max %d with size %d",do_max,data_size);

#ifdef USE_SERVER
    if (bind(s.socket, (struct sockaddr *) &s.sa.in, s.addrlen) )
        SAYPX("bind %s",s.to_string);
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
    start = clock();
    magic_place = (unsigned int *) &DATA[sizeof(DATA) - 10];
    DATA[sizeof(DATA) - 1] = '\n';
    *magic_place = MAGIC;
    while (do_max == 0 || i < do_max) {
        int rc;
        #ifdef USE_SERVER
        int expected= 0;
        #endif

        i++;
        #ifdef USE_SERVER
            #ifdef RANDOM_DEATH
            if (s.type != SOCK_DGRAM && (random() % 100000000) == 42)
                SAYX(EXIT_SUCCESS,"death decided by chance");
            #endif
            *magic_place = 0x11111111;
            if (recv(fd,&expected,4,MSG_WAITALL) != 4)
                SAYPX("recv");
            if (expected != sizeof(DATA))
                SAYX(EXIT_FAILURE,"expected: %zu, got: %d",sizeof(DATA),expected);
            if ((rc = recv(fd,DATA,sizeof(DATA),MSG_WAITALL)) <= 0) {
                if (rc == 0) // shutdown
                    break;
                SAYPX("recv");
            }
            if (*magic_place != MAGIC)
                SAYX(EXIT_FAILURE,"magic not found in the received packet %x recv: %d, data_size: %d",*magic_place,rc,data_size);
        #else
            rc = sendto(fd,DATA,sizeof(DATA),0,(struct sockaddr*) &s.sa.in,s.addrlen);
            if (rc < 0)
                SAYPX("sendto");
        #endif
    }
    ms = (clock() - start) * 1000 / CLOCKS_PER_SEC;
    _D(PREFIX " %lu with size %d for %lu ms (expected %f per second)",i,data_size,ms,i/((double)ms/1000));
    return(0);
}
