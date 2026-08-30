#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#define PWM_SWITCHING_FREQUENCY_HZ 20000U
#define PWM_DEADTIME_NS            200U

#define PWM_AH_POLARITY            0U
#define PWM_AL_POLARITY            0U
#define PWM_BH_POLARITY            0U
#define PWM_BL_POLARITY            0U

void init_pins_current_control(void);

void task_current_control(void);

#endif // CURRENT_CONTROL_H