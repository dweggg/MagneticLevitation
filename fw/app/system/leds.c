#include "leds.h"
#include "ch32fun.h"

#include "tasks.h"
#include "fsm.h"
#include "pinout.h"
#include "usb_pd.h"
#include "usb_cdc.h"

#define LEDS_FAST_INTERVAL_MS 300U // ms on, ms off (600ms full cycle)
#define LEDS_SLOW_INTERVAL_MS 1000U // ms on, ms off (2s full cycle)

// Forward declarations
static void leds_blink(uint8_t pin, uint32_t interval_ms);
static inline void leds_turn_on(uint8_t pin);
static inline void leds_turn_off(uint8_t pin);

void leds_init(void){
    funPinMode(PIN_LED_RED, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_LED_WHITE, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
}

// Task
void leds_task(void){

	/* White LED */
    leds_blink(PIN_LED_WHITE, LEDS_SLOW_INTERVAL_MS); // Heartbeat, if blinking slows down or stops it means the scheduler/CPU is struggling
    
	/* Red LED */
	if (fsm_state() == FSM_FAULT) {
        leds_blink(PIN_LED_RED, LEDS_FAST_INTERVAL_MS);
	} else if (fsm_state() == FSM_POSITION_CONTROL) {
        leds_blink(PIN_LED_RED, LEDS_SLOW_INTERVAL_MS);
	} else if (fsm_state() == FSM_CURRENT_CONTROL) {
        leds_turn_on(PIN_LED_RED);
	} else {
        leds_turn_off(PIN_LED_RED);
	}
}

// Helpers
static void leds_blink(uint8_t pin, uint32_t interval_ms)
{
    static uint32_t counter_white;
    static uint32_t counter_red;
    static uint8_t state_white;
    static uint8_t state_red;

    uint32_t *counter;
    uint8_t *state;

    if (pin == PIN_LED_WHITE) {
        counter = &counter_white;
        state = &state_white;
    } else {
        counter = &counter_red;
        state = &state_red;
    }

    *counter += TASK_LEDS_PERIOD_MS;

    if (*counter >= interval_ms) {
        *counter = 0;
        *state ^= 1;
        funDigitalWrite(pin, *state);
    }
}

static inline void leds_turn_on(uint8_t pin){
	funDigitalWrite(pin, 1);
}

static inline void leds_turn_off(uint8_t pin){
	funDigitalWrite(pin, 0);
}
