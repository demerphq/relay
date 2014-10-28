#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdint.h>

#include "relay_threads.h"
#include "relay_common.h"

#define RELAY_STARTING	1<<0
#define RELAY_RUNNING   1<<1
#define RELAY_RELOADING 1<<2
#define RELAY_STOPPING  1<<3

void control_set_bits(uint32_t c);
void control_unset_bits(uint32_t c);
uint32_t control_get_bits(void);
uint32_t control_is_not(uint32_t c);
uint32_t control_is(uint32_t c);

#endif				/* #ifndef RELAY_CONTROL_H */
