#include "fsm.h"
#include "pinout.h"
#include "vars.h"
#include "scheduler.h"

static fsm_state_t state = FSM_INIT;
static uint8_t enable;      /* host-writable */
static uint8_t control_mode = FSM_MODE_AUTO_POWER;
static uint8_t cpu_usage;   /* host-readable */
static uint8_t state_monitor;
static uint8_t fault_reasons;

void fsm_init(void){
	VARS_PARAM("enable", VAR_U8, &enable);
    VARS_PARAM("control_mode", VAR_U8, &control_mode);
	VARS_MONITOR("cpu", VAR_U8, &cpu_usage, VARS_STREAM_NONE);
    VARS_MONITOR("fsm_state", VAR_U8, &state_monitor, VARS_STREAM_NONE);
    VARS_MONITOR("fault_reasons", VAR_U8, &fault_reasons, VARS_STREAM_NONE);

	funPinMode(PIN_RST_BUTTON, GPIO_CFGLR_IN_PUPD);   // input with pull-up
	funDigitalWrite(PIN_RST_BUTTON, FUN_HIGH);        // enable pull-up even though we have hardware pull-up

	funPinMode(PIN_GD_EN, GPIO_CFGLR_OUT_10Mhz_PP);    // output, push-pull
	funDigitalWrite(PIN_GD_EN, FUN_LOW); 		 // Disable 12V at startup

}

void fsm_task(void)
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
            if (control_mode == FSM_MODE_MANUAL_CURRENT) {
                state = FSM_CURRENT_CONTROL;
            } else if (control_mode <= FSM_MODE_MANUAL_CURRENT) {
                state = FSM_POSITION_CONTROL;
            } else {
                fsm_raise_fault(FAULT_INVALID_MODE);
            }
        }
        break;
    }

    case FSM_POSITION_CONTROL:
        funDigitalWrite(PIN_GD_EN,
            (enable != 0U && control_mode == FSM_MODE_MANUAL_POSITION) ? FUN_HIGH : FUN_LOW);
        if (enable == 0U) {
            state = FSM_IDLE;
        } else if (control_mode == FSM_MODE_MANUAL_CURRENT) {
            state = FSM_CURRENT_CONTROL;
        } else if (control_mode > FSM_MODE_MANUAL_CURRENT) {
            fsm_raise_fault(FAULT_INVALID_MODE);
        }
        break;

    case FSM_CURRENT_CONTROL: {
        if (enable == 0U) {
            state = FSM_IDLE;
            break;
        }

        if (control_mode != FSM_MODE_MANUAL_CURRENT) {
            if (control_mode > FSM_MODE_MANUAL_CURRENT) {
                fsm_raise_fault(FAULT_INVALID_MODE);
            } else {
                state = FSM_POSITION_CONTROL;
            }
            funDigitalWrite(PIN_GD_EN, FUN_LOW);
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
        fsm_raise_fault(FAULT_INVALID_MODE);
        break;
    }

    state_monitor = (uint8_t)state;
}

fsm_state_t fsm_state(void){
	return state;
}

fsm_control_mode_t fsm_control_mode(void){
    return (fsm_control_mode_t)control_mode;
}

void fsm_raise_fault(fsm_fault_t reason){
    if (reason == FAULT_NONE) {
        return;
    }

    fault_reasons |= (uint8_t)reason;
    state = FSM_FAULT;
    funDigitalWrite(PIN_GD_EN, FUN_LOW);
}

uint8_t fsm_fault_reasons(void){
    return fault_reasons;
}
