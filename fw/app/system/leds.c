#include "leds.h"
#include "ch32fun.h"

#include "tasks.h"
#include "fsm.h"
#include "pinout.h"
#include "usb_pd.h"
#include "usb_cdc.h"

#define FAST 300U //ms on, ms off (600ms full cycle)
#define SLOW 1000U //ms on, ms off (2s full cycle)

#define LED_WHITE PIN_LED_WHITE // for legibility
#define LED_RED PIN_LED_RED

// Forward declarations
static void blink(uint8_t pin, uint32_t interval_ms);
static inline void turn_on(uint8_t pin);
static inline void turn_off(uint8_t pin);

// Task
void task_leds(void){

	/* White LED */
	if (usb_cdc_debug_is_active()) {
		blink(LED_WHITE, SLOW);
	} else if (usb_pd_negotiating()) {
		blink(LED_WHITE, FAST);
	} else {
		turn_off(LED_WHITE);
	}

	/* Red LED */
	if (fsm_state() == FSM_FAULT) {
		blink(LED_RED, FAST);
	} else if (fsm_state() == FSM_CURRENT_CONTROL) {
		turn_on(LED_RED);
	} else {
		turn_off(LED_RED);
	}
}

// Helpers
static void blink(uint8_t pin, uint32_t interval_ms)
{
    static uint32_t counter_white;
    static uint32_t counter_red;
    static uint8_t state_white;
    static uint8_t state_red;

    uint32_t *counter;
    uint8_t *state;

    if (pin == LED_WHITE) {
        counter = &counter_white;
        state = &state_white;
    } else {
        counter = &counter_red;
        state = &state_red;
    }

    *counter += TASK_LEDS_MS;

    if (*counter >= interval_ms) {
        *counter = 0;
        *state ^= 1;
        funDigitalWrite(pin, *state);
    }
}

static inline void turn_on(uint8_t pin){
	funDigitalWrite(pin, 1);
}

static inline void turn_off(uint8_t pin){
	funDigitalWrite(pin, 0);
}
