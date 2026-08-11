#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize LED hardware. */
void leds_init(void);

/** Set LED state bitmask. */
void leds_set(uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* LEDS_H */