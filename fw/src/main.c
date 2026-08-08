#include "main.h"
#include "pinout.h"
#include "scheduler.h"

uint32_t task_led_hz = 1; // task a will run at 1 Hz
void task_led(void) {
    static uint8_t led_state = 0;
    funDigitalWrite(PIN_LED_WHITE, led_state);
    led_state ^= 1;
    funDigitalWrite(PIN_LED_RED, led_state);
}

static uint32_t systick_get_ticks(void)
{
    return (uint32_t)SysTick->CNT;
}

static void systick_init(void)
{
    SysTick->CNT = 0;
    SysTick->CMP = (uint64_t)-1;    // never interrupt
    SysTick->CTLR =
          (1 << 0)    // enable counter
        | (1 << 2);   // HCLK source
}

int main(void)
{
	SystemInit();
    funGpioInitAll();
    funPinMode(PIN_LED_RED, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funPinMode(PIN_LED_WHITE, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);

	systick_init();

	scheduler_init(
	    systick_get_ticks,
	    FUNCONF_SYSTEM_CORE_CLOCK
	);

    scheduler_add_task(task_led, task_led_hz);
    scheduler_run();
    
    while (1)
	{

	}
}