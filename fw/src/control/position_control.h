#ifndef POSITION_CONTROL_H
#define POSITION_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize position control module. */
void position_control_init(void);

/** Update position control loop with target and measured positions. */
void position_control_update(int16_t target_pos, int16_t measured_pos);

#ifdef __cplusplus
}
#endif

#endif /* POSITION_CONTROL_H */
