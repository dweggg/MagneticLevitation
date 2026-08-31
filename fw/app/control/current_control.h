#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#include <fix16.h>

#define PWM_SWITCHING_FREQUENCY_HZ 100000U
#define PWM_DEADTIME_NS            200U

#define PWM_AH_POLARITY            0U
#define PWM_AL_POLARITY            1U
#define PWM_BH_POLARITY            0U
#define PWM_BL_POLARITY            1U

uint16_t current_control_duty_to_ticks(fix16_t duty_q16);

void init_pins_current_control(void);

void task_current_control(void);

#endif // CURRENT_CONTROL_H