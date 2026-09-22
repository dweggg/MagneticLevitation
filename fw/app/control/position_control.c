#include "position_control.h"
#include "pinout.h"
#include "vars.h"
#include "current_control.h"

static uint16_t mag_raw;

void init_pins_position_control(void){
    var_monitor("mag_raw", VAR_U16, &mag_raw, STREAM_NONE);
}

void task_position_control(void){
    mag_raw = get_mag_meas_raw();
}
