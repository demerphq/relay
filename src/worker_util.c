#include "log.h"
#include "worker_util.h"

relay_socket_t *open_output_socket_eventually(struct worker_base *base)
{
    const config_t *config = base->config;
    relay_socket_t *sck = NULL;
    int nap = config->sleep_after_disaster_millisec;

    while (!RELAY_ATOMIC_READ(base->stopping) && !sck) {
	if (open_socket(&base->output_socket, DO_CONNECT, config->server_socket_sndbuf_bytes, 0)) {
	    sck = &base->output_socket;
	} else {
	    /* no socket - wait a while, double the wait, and then redo the loop */
	    SAY("waiting %d millisec to retry socket %s", nap, base->output_socket.to_string);
	    worker_wait_millisec(nap);
	    nap = 2 * nap + (time(NULL) & 31);	/* "Random" fuzz of up to 0.031s. */
	}
    }

    return sck;
}
