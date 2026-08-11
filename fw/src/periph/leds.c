#include "leds.h"
#include <stdio.h>

void leds_init(void)
{
    /* TODO: configure GPIOs for LEDs */
    (void)printf("leds_init\n");
}

void leds_set(uint32_t mask)
{
    /* TODO: set/clear LED GPIOs according to mask */
    (void)mask;
}
