#ifndef SETPOINT_H
#define SETPOINT_H

#include <stdint.h>
#include <fix16.h>

void init_pins_setpoint(void);

void task_setpoint(void);

fix16_t setpoint_get_current_sp(void);
fix16_t setpoint_get_position_sp(void);
fix16_t setpoint_get_power_budget_w(void);
fix16_t setpoint_get_temperature_c(void);

#endif // SETPOINT_H