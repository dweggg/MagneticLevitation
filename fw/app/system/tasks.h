#ifndef TASKS_H
#define TASKS_H

#include <fix16.h>

/* Task frequencies in Hertz. */
#define TASK_CURRENT_CONTROL_HZ  5000U
#define TASK_POSITION_CONTROL_HZ 100U
#define TASK_SETPOINT_HZ         100U
#define TASK_USB_PD_HZ           1000U
#define TASK_USB_CDC_HZ          1000U
#define TASK_FSM_HZ              1000U
#define TASK_LEDS_HZ             10U

/* Task periods in milliseconds. */
#define TASK_POSITION_CONTROL_PERIOD_MS (1000U / TASK_POSITION_CONTROL_HZ)
#define TASK_SETPOINT_PERIOD_MS         (1000U / TASK_SETPOINT_HZ)
#define TASK_USB_PD_PERIOD_MS           (1000U / TASK_USB_PD_HZ)
#define TASK_USB_CDC_PERIOD_MS          (1000U / TASK_USB_CDC_HZ)
#define TASK_FSM_PERIOD_MS              (1000U / TASK_FSM_HZ)
#define TASK_LEDS_PERIOD_MS             (1000U / TASK_LEDS_HZ)

/* Task periods in seconds, represented as Q16.16 fixed-point values. */
#define TASK_CURRENT_CONTROL_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_CURRENT_CONTROL_HZ))

#define TASK_POSITION_CONTROL_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_POSITION_CONTROL_HZ))

#define TASK_SETPOINT_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_SETPOINT_HZ))

#define TASK_USB_PD_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_USB_PD_HZ))

#define TASK_USB_CDC_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_USB_CDC_HZ))

#define TASK_FSM_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_FSM_HZ))

#define TASK_LEDS_PERIOD_S_Q16 \
    fix16_div(fix16_one, fix16_from_int(TASK_LEDS_HZ))

void tasks_init_modules(void);

/*
Initializes all tasks needed for the platform. These are:

- current_control_task (5kHz)
- position_control_task (100Hz)
- setpoint_task (100Hz)
- usb_pd_task (1kHz)
- usb_cdc_task (1kHz)
- fsm_task (1kHz)
- leds_task (10Hz)
*/
void tasks_init_scheduler(void);


#endif // TASKS_H
