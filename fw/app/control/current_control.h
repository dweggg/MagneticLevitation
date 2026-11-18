#ifndef CURRENT_CONTROL_H
#define CURRENT_CONTROL_H

#include <fix16.h>

#define CURRENT_CONTROL_PWM_SWITCHING_FREQUENCY_HZ 20000U
#define CURRENT_CONTROL_PWM_DEADTIME_NS            200U

#define CURRENT_CONTROL_PWM_AH_POLARITY            0U
#define CURRENT_CONTROL_PWM_AL_POLARITY            1U
#define CURRENT_CONTROL_PWM_BH_POLARITY            0U
#define CURRENT_CONTROL_PWM_BL_POLARITY            1U

/* Gains */
#define BOARD_SUPPLY_VOLTAGE_V 4.38f // This board had the wrong buck PN so we have 4.38V instead of 3.3V

#define CURRENT_CONTROL_ADC_V_MEAS_GAIN F16((BOARD_SUPPLY_VOLTAGE_V/4095.0f)*(5.1f+33.0f)/5.1f) // Simple voltage divider with 33k top and 5.1k bottom
#define CURRENT_CONTROL_ADC_I_MEAS_GAIN F16((1.0f/50.0e-3f)*(BOARD_SUPPLY_VOLTAGE_V/4095.0f)) // INA199A1 is 50V/V, with a 1mOhm shunt

uint16_t current_control_duty_to_ticks(fix16_t duty_q16);

void current_control_init(void);

void current_control_task(void);


// All ADC channels are synced with PWM, so we need
// getters for the measurements used in other tasks.
uint16_t current_control_get_magnetic_field_adc_raw(void);
uint16_t current_control_get_temp_adc_raw(void);

#endif // CURRENT_CONTROL_H