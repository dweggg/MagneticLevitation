#include "tasks.h"
#include "scheduler.h"
#include "ch32fun.h"
#include "pinout.h"

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


	systick_init();

	scheduler_init(
	    systick_get_ticks,
	    FUNCONF_SYSTEM_CORE_CLOCK
	);

    init_pins();
    scheduler_init_tasks();

    scheduler_run();
    
    while (1)
	{
		// should never reach here!
	}
}