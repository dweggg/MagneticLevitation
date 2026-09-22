#include "fsm.h"
#include "pinout.h"
#include "vars.h"
#include "scheduler.h"

static fsm_state_t state = FSM_INIT;
static uint8_t enable;      /* host-writable */
static uint8_t cpu_usage;   /* host-readable */

void init_pins_fsm(void){
	var_param("enable", VAR_U8, &enable);
	var_monitor("cpu", VAR_U8, &cpu_usage, STREAM_NONE);

	funPinMode(PIN_RST_BUTTON, GPIO_CFGLR_IN_PUPD);   // input with pull-up
	funDigitalWrite(PIN_RST_BUTTON, FUN_HIGH);        // enable pull-up even though we have hardware pull-up

	funPinMode(PIN_GD_EN, GPIO_CFGLR_OUT_10Mhz_PP);    // output, push-pull
	funDigitalWrite(PIN_GD_EN, FUN_LOW); 		 // Disable 12V at startup

}

void task_fsm(void)
{
    /* runs every tick regardless of state */
    cpu_usage = scheduler_get_cpu();

    /* global override: reset button works from any state */
    if (funDigitalRead(PIN_RST_BUTTON) == FUN_LOW) {
        state = FSM_RESET;
    }

    switch (state) {

    case FSM_INIT:
        /* one-time startup work goes here */
        state = FSM_IDLE;
        break;

    case FSM_RESET:
        NVIC_SystemReset();
        break; /* never reached */

    case FSM_IDLE: {
        funDigitalWrite(PIN_GD_EN, FUN_LOW);

        if (enable != 0U) {
            state = FSM_CURRENT_CONTROL;
        }
        break;
    }

    case FSM_CURRENT_CONTROL: {
        if (enable == 0U) {
            state = FSM_IDLE;
            break;
        }

        funDigitalWrite(PIN_GD_EN, FUN_HIGH);
        break;
    }

    case FSM_FAULT:
        funDigitalWrite(PIN_GD_EN, FUN_LOW);
        /* stays here until reset button is pressed */
        break;

    default:
        state = FSM_FAULT;
        break;
    }
}

fsm_state_t fsm_state(void){
	return state;
}
