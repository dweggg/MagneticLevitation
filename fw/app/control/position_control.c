#include "position_control.h"
#include "pinout.h"
#include "vars.h"
#include "current_control.h"
#include "scheduler.h"
#include "tasks.h"
#include "setpoint.h"

static fix16_t B_T = 0;
static uint16_t mag_raw = 0;
static fix16_t position_sp_active;
static stream_id_t position_stream = STREAM_NONE;

void get_B_T(void){
    mag_raw = get_mag_meas_raw();
    int32_t delta = (int32_t)mag_raw - ADC_ZERO_FIELD;
    B_T = (fix16_t)((delta << 16) / COUNTS_PER_TESLA);
}

void init_pins_position_control(void){
    position_stream = stream_create("position", TASK_POSITION_CONTROL_HZ);

    var_monitor("B_T", VAR_F16, &B_T, position_stream);
    var_monitor("mag_raw", VAR_U16, &mag_raw, position_stream);
    var_monitor("position_sp", VAR_F16, &position_sp_active, position_stream);

}

void task_position_control(void){
    const uint32_t tick = scheduler_get_tick();
    get_B_T();
    position_sp_active = setpoint_get_position_sp();
    stream_emit_at(position_stream, tick);
}


