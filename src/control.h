#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include "relay_threads.h"
#include "relay_common.h"
#include <stdint.h>

#define RELAY_STOP    1
#define RELAY_RELOAD  2

void set_control_bits(uint32_t v);
void unset_control_bits(uint32_t v);
void set_stopped(void);
uint32_t get_control_val(void);
uint32_t not_stopped(void);
uint32_t is_stopped(void);

#endif /* #ifndef RELAY_CONTROL_H */
