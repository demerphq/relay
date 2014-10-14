#include "control.h"
#include "relay_threads.h"

volatile uint32_t CONTROL = 0;

void set_control_bits(uint32_t v)
{
    RELAY_ATOMIC_OR(CONTROL, v);
}

void unset_control_bits(uint32_t v)
{
    v = ~v;
    RELAY_ATOMIC_AND(CONTROL, v);
}

void set_stopped()
{
    set_control_bits(STOP);
}

uint32_t get_control_val()
{
    return RELAY_ATOMIC_READ(CONTROL);
}

uint32_t not_stopped()
{
    uint32_t v = RELAY_ATOMIC_READ(CONTROL);
    return (v & STOP) == 0;
}

uint32_t is_stopped()
{
    uint32_t v = RELAY_ATOMIC_READ(CONTROL);
    return (v & STOP) == STOP;
}
