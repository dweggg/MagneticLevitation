#include "fsm.h"
#include "pinout.h"
#include "parameters.h"
#include "scheduler.h"

static fsm_state_t state = FSM_INIT;

void init_pins_fsm(void){
	funPinMode(PIN_RST_BUTTON, GPIO_CFGLR_IN_PUPD);   // input with pull-up
	funDigitalWrite(PIN_RST_BUTTON, FUN_HIGH);        // enable pull-up even though we have hardware pull-up
}

void task_fsm(void){
	uint8_t enable = 0U;

	if (funDigitalRead(PIN_RST_BUTTON) == FUN_LOW) {
		state = FSM_RESET;
		NVIC_SystemReset();
		return;
	}

	if (parameters_fetch(PARAM_ID_ENABLE, &enable, sizeof(enable)) < 0) {
		enable = 0U;
	}
	state = (enable != 0U) ? FSM_CURRENT_CONTROL : FSM_IDLE;

	uint8_t cpu_usage = 0U;

	cpu_usage = scheduler_get_cpu();
	// publish to parameters
	parameters_publish(PARAM_ID_CPU, &cpu_usage);

}

fsm_state_t fsm_state(void){
	return state;
}
