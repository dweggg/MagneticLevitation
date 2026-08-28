#include "setpoint.h"
#include "protocol.h"
#include "ch32fun.h"
#include "pinout.h"
#include "protocol.h"

static volatile uint16_t temp_meas_raw;
uint16_t setpoint_get_temp_raw(void);

void init_pins_setpoint(void){
    funPinMode(PIN_TEMP_MEAS, GPIO_CFGLR_IN_ANALOG);
    funAnalogInit();
}

void task_setpoint(void){
    temp_meas_raw = (uint16_t)funAnalogRead(PIN_TEMP_MEAS);
    LOG("TEMP: %u", (unsigned int)temp_meas_raw);
}

uint16_t setpoint_get_temp_raw(void){
    return temp_meas_raw;
}
