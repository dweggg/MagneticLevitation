#include "../inc/tasks.h"
#include "../inc/leds.h"
#include "../inc/fsm.h"
#include "../inc/usb_pd.h"
#include <stdio.h>

void tasks_init(void)
{
    leds_init();
    fsm_init();
    (void)printf("tasks_init\n");
}

void tasks_run(void)
{
    /* call periodic modules */
    usb_pd_poll();
}
