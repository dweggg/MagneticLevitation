#include "position_control.h"
#include "pinout.h"
#include "parameters.h"
#include "current_control.h"

void init_pins_position_control(void){
}

void task_position_control(void){
    const uint16_t raw = get_mag_meas_raw();
    parameters_publish(PARAM_ID_MAG_RAW, &raw);

}
