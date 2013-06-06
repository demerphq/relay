#include "relay.h"
static struct throttle {
    time_t last_seen;
    int count;
} THROTTLE[255];

void throttle_init_static(void) {
    bzero(THROTTLE,sizeof(THROTTLE));
}

int is_throttled(unsigned char type) {
    time_t now = time(NULL);
    int rc = 0;
    if (now - THROTTLE[type].last_seen < THROTTLE_INTERVAL)
        rc = 1;
    THROTTLE[type].last_seen = time(NULL);
    return rc;
}

void t_fprintf(unsigned char type, FILE *stream, const char *format, ...) {
    if (is_throttled(type))
        return;
    va_list va;
    va_start(va, format);
    vfprintf(stream,format,va);
    va_end(va);
}
