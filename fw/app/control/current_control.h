#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#include <fix16.h>

#define PWM_SWITCHING_FREQUENCY_HZ 20000U
#define PWM_DEADTIME_NS            200U

#define PWM_AH_POLARITY            0U
#define PWM_AL_POLARITY            1U
#define PWM_BH_POLARITY            0U
#define PWM_BL_POLARITY            1U

/* Gains */
#define VCC                        4.38f // This board had the wrong buck PN so we have 4.38V instead of 3.3V

#define ADC_V_MEAS_GAIN            F16((VCC/4095.0f)*(5.1f+33.0f)/5.1f) // Simple voltage divider with 33k top and 5.1k bottom
#define ADC_I_MEAS_GAIN            F16((1.0f/50.0e-3f)*(VCC/4095.0f)) // INA199A1 is 50V/V, with a 1mOhm shunt

uint16_t current_control_duty_to_ticks(fix16_t duty_q16);

void init_pins_current_control(void);

void task_current_control(void);


// All ADC channels are synced with PWM, so we need
// getters for the measurements used in other tasks.
uint16_t get_mag_meas_raw(void);
uint16_t get_temp_meas_raw(void);

#endif // CURRENT_CONTROL_H