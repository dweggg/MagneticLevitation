#include "tasks.h"
#include "scheduler.h"
#include "protocol.h"


#include "fsm.h"
#include "leds.h"
#include "usb_cdc.h"
#include "usb_pd.h"
#include "current_control.h"
#include "position_control.h"

void init_pins(void){
	init_pins_current_control();
	init_pins_position_control();
	init_pins_usb_pd();
	init_pins_usb_cdc();
	init_pins_fsm();
	init_pins_leds();

}

void scheduler_init_tasks(void){
    scheduler_add_task(task_current_control, TASK_CURRENT_CONTROL_HZ);
	scheduler_add_task(task_position_control, TASK_POSITION_CONTROL_HZ);
	scheduler_add_task(task_usb_pd, TASK_USB_PD_HZ);
	scheduler_add_task(task_usb_cdc, TASK_USB_CDC_HZ);
	scheduler_add_task(task_fsm, TASK_FSM_HZ);
	scheduler_add_task(task_leds, TASK_LEDS_HZ);
	LOG("Initialization complete");
}
