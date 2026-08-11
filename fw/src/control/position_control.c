#include "position_control.h"
#include <stdio.h>

void position_control_init(void)
{
    /* TODO: initialize position sensors, filters, and state */
    (void)printf("position_control_init\n");
}

void position_control_update(int16_t target_pos, int16_t measured_pos)
{
    /* TODO: implement position control (cascade to current control, etc.) */
    (void)target_pos;
    (void)measured_pos;
}
