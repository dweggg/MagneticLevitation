#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize current control module. */
void current_control_init(void);

/** Update current control loop with target and measured currents. */
void current_control_update(int16_t target_mA, int16_t measured_mA);

#ifdef __cplusplus
}
#endif

#endif /* CURRENT_CONTROL_H */
