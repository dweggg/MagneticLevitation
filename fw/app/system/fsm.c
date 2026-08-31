#include "fsm.h"
#include "pinout.h"
#include "parameters.h"

static fsm_state_t state = FSM_INIT;

void init_pins_fsm(void){
	funPinMode(PIN_RST_BUTTON, GPIO_CFGLR_IN_PUPD);   // input with pull-up
	funDigitalWrite(PIN_RST_BUTTON, FUN_HIGH);        // enable pull-up even though we have hardware pull-up
}

void task_fsm(void){
	if (funDigitalRead(PIN_RST_BUTTON) == FUN_LOW) {
		state = FSM_RESET;
		NVIC_SystemReset();
		return;
	}

	state = (parameters_get_enable() != 0U) ? FSM_CURRENT_CONTROL : FSM_IDLE;
}

fsm_state_t fsm_state(void){
	return state;
}
