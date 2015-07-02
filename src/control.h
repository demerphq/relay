#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdint.h>

#define RELAY_STARTING	1<<0
#define RELAY_RUNNING   1<<1
#define RELAY_RELOADING 1<<2
#define RELAY_STOPPING  1<<3

void control_set_bits(uint32_t c);
void control_unset_bits(uint32_t c);
uint32_t control_get_bits(void);
uint32_t control_is_not(uint32_t c);
uint32_t control_is(uint32_t c);
uint32_t control_is_not_one_of(uint32_t c);
uint32_t control_is_one_of(uint32_t c);
void control_exit(int rc);
int control_exit_code(void);

#endif                          /* #ifndef RELAY_CONTROL_H */
