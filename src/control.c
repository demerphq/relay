#include "control.h"

#include "relay_threads.h"

static volatile uint32_t __control = 0;

void control_set_bits(uint32_t c)
{
    RELAY_ATOMIC_OR(__control, c);
}

void control_unset_bits(uint32_t c)
{
    c = ~c;
    RELAY_ATOMIC_AND(__control, c);
}

uint32_t control_get_bits(void)
{
    return RELAY_ATOMIC_READ(__control);
}

uint32_t control_is_not(uint32_t c)
{
    uint32_t v = RELAY_ATOMIC_READ(__control);
    return (v & c) == 0;
}

uint32_t control_is(uint32_t c)
{
    uint32_t v = RELAY_ATOMIC_READ(__control);
    return (v & c) == c;
}

void control_exit(int rc)
{
    uint32_t c = control_get_bits();

    if (c & RELAY_RUNNING) {
	WARN("Running: exit(%d), called, stopping\n", rc);
    } else if (c & RELAY_STARTING) {
	WARN("Starting: exit(%d) called, exiting\n", rc);
	exit(rc);
    } else {
	WARN("control_exit: Unexpected state %#x: exit(%d) called, stopping\n", c, rc);
    }
    if ((c & RELAY_STOPPING) == 0) {
	control_set_bits(RELAY_STOPPING);
    }
}
