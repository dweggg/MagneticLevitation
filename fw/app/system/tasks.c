#include "tasks.h"
#include "scheduler.h"
#include "protocol.h"
#include "vars.h"

#include "fsm.h"
#include "leds.h"
#include "usb_cdc.h"
#include "usb_pd.h"
#include "current_control.h"
#include "position_control.h"
#include "setpoint.h"

void tasks_init_modules(void){
	vars_init();   /* registry must exist before any module registers variables */
	current_control_init();
	position_control_init();
	setpoint_init();
	usb_pd_init();
	usb_cdc_init();
	fsm_init();
	leds_init();

}

void tasks_init_scheduler(void){
	scheduler_add_task(current_control_task, TASK_CURRENT_CONTROL_HZ);
	scheduler_add_task(position_control_task, TASK_POSITION_CONTROL_HZ);
	scheduler_add_task(setpoint_task, TASK_SETPOINT_HZ);
	scheduler_add_task(usb_pd_task, TASK_USB_PD_HZ);
	scheduler_add_task(usb_cdc_task, TASK_USB_CDC_HZ);
	scheduler_add_task(fsm_task, TASK_FSM_HZ);
	scheduler_add_task(leds_task, TASK_LEDS_HZ);
	PROTOCOL_LOG("Initialization complete");
}
