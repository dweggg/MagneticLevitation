#ifndef TASKS_H
#define TASKS_H

#include <fix16.h>

/* Task frequencies in Hertz. */
#define TASK_CURRENT_CONTROL_HZ 10000
#define TASK_POSITION_CONTROL_HZ 100
#define TASK_SETPOINT_HZ         100
#define TASK_USB_PD_HZ           1000
#define TASK_USB_CDC_HZ          100
#define TASK_FSM_HZ              1000
#define TASK_LEDS_HZ             10

/* Task periods in milliseconds. */
//#define TASK_CURRENT_CONTROL_MS  (1000U / TASK_CURRENT_CONTROL_HZ) //sub-ms, commented out
#define TASK_POSITION_CONTROL_MS (1000U / TASK_POSITION_CONTROL_HZ)
#define TASK_SETPOINT_MS         (1000U / TASK_SETPOINT_HZ)
#define TASK_USB_PD_MS           (1000U / TASK_USB_PD_HZ)
#define TASK_USB_CDC_MS          (1000U / TASK_USB_CDC_HZ)
#define TASK_FSM_MS              (1000U / TASK_FSM_HZ)
#define TASK_LEDS_MS             (1000U / TASK_LEDS_HZ)

/* Task periods in seconds, represented as Q16.16 fixed-point values. */
#define TASK_CURRENT_CONTROL_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_CURRENT_CONTROL_HZ))

#define TASK_POSITION_CONTROL_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_POSITION_CONTROL_HZ))

#define TASK_SETPOINT_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_SETPOINT_HZ))

#define TASK_USB_PD_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_USB_PD_HZ))

#define TASK_USB_CDC_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_USB_CDC_HZ))

#define TASK_FSM_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_FSM_HZ))

#define TASK_LEDS_S_F16 \
    fix16_div(fix16_one, fix16_from_int(TASK_LEDS_HZ))

// Calls a init_pins_X() for each individual task.
void init_pins(void);

/*
Initializes all tasks needed for the platform. These are:

- task_current_control (10kHz)
- task_position_control (100Hz)
- task_setpoint (100Hz)
- task_usb_pd (1kHz)
- task_usb_cdc (100Hz)
- task_fsm (1kHz)
- task_leds (10Hz)
*/
void scheduler_init_tasks(void);


#endif // TASKS_H
