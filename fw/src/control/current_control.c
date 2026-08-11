#include "current_control.h"
#include <stdio.h>

void current_control_init(void)
{
    /* TODO: initialize timers, ADC references, and control state */
    (void)printf("current_control_init\n");
}

void current_control_update(int16_t target_mA, int16_t measured_mA)
{
    /* TODO: implement PID or other current control algorithm */
    (void)target_mA;
    (void)measured_mA;
}
