#ifndef _STATS_H
#define _STATS_H

#include "relay_common.h"
#include "relay_threads.h"
#include "setproctitle.h"
#include <stdio.h>

#define MAX_BUF_LEN 128

#define STATSfmt "%lu"
typedef uint64_t stats_count_t;

void inc_sent_count();
void inc_received_count();
void mark_second_elapsed();

#endif
