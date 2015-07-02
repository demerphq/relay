#include "control.h"

#include "global.h"
#include "log.h"
#include "relay_threads.h"

void control_set_bits(uint32_t c)
{
    RELAY_ATOMIC_OR(GLOBAL.control, c);
}

void control_unset_bits(uint32_t c)
{
    c = ~c;
    RELAY_ATOMIC_AND(GLOBAL.control, c);
}

uint32_t control_get_bits(void)
{
    return RELAY_ATOMIC_READ(GLOBAL.control);
}

uint32_t control_is_not(uint32_t c)
{
    uint32_t v = RELAY_ATOMIC_READ(GLOBAL.control);
    return (v & c) == 0;
}

uint32_t control_is(uint32_t c)
{
    uint32_t v = RELAY_ATOMIC_READ(GLOBAL.control);
    return (v & c) == c;
}

uint32_t control_is_not_one_of(uint32_t c)
{
    uint32_t v = RELAY_ATOMIC_READ(GLOBAL.control);
    return (v & c) == 0;
}

uint32_t control_is_one_of(uint32_t c)
{
    uint32_t v = RELAY_ATOMIC_READ(GLOBAL.control);
    return (v & c) != 0;
}

void control_exit(int rc)
{
    uint32_t c = control_get_bits();

    if (c & RELAY_RUNNING) {
        WARN("Running: stopping");
    } else if (c & RELAY_STARTING) {
        WARN("Starting: exit(%d) called, exiting", rc);
        exit(rc);
    } else if (c & RELAY_STOPPING) {
        WARN("Already stopping");
    } else {
        WARN("Unexpected state %#x: stopping", c);
    }
    WARN("Stopping: exit(%d) called", rc);
    GLOBAL.exit_code = rc;
    if ((c & RELAY_STOPPING) == 0) {
        control_set_bits(RELAY_STOPPING);
    }
}

int control_exit_code(void)
{
    return GLOBAL.exit_code;
}
