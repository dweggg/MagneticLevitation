#ifndef SETPOINT_H
#define SETPOINT_H

#include <stdint.h>
#include <fix16.h>

void setpoint_init(void);

void setpoint_task(void);

fix16_t setpoint_get_i_sp(void);
fix16_t setpoint_get_x_sp(void);
fix16_t setpoint_get_power_budget_w(void);
fix16_t setpoint_get_temp_c(void);

#endif // SETPOINT_H