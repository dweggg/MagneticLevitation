#ifndef TASKS
#define TASKS

#define TASK_CURRENT_CONTROL_HZ 10000
#define TASK_POSITION_CONTROL_HZ 100
#define TASK_USB_PD_HZ 1000
#define TASK_USB_CDC_HZ 100
#define TASK_FSM_HZ 1000
#define TASK_LEDS_HZ 10

/*
Initializes all tasks needed for the platform. These are:
- _task_current_control (10kHz)
- _task_position_control (100Hz)
- _task_usb_pd (1kHz)
- _task_usb_cdc (100Hz)
- _task_fsm (1kHz)
- _task_leds (10Hz)
*/
void scheduler_init_tasks(void);

#endif // TASKS