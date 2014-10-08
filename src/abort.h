#include "relay_threads.h"
#include "relay_common.h"
#include <stdint.h>

#define STOP    1
#define RELOAD  2

void set_abort_bits(uint32_t v);
void unset_abort_bits(uint32_t v);
void set_aborted();
uint32_t get_abort_val();
uint32_t not_aborted();
uint32_t is_aborted();
