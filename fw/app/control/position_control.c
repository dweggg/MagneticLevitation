#include "position_control.h"
#include "pinout.h"
#include "vars.h"
#include "current_control.h"
#include "scheduler.h"
#include "tasks.h"
#include "setpoint.h"

static uint16_t b_full_adc_raw;
static uint8_t b_full_valid;
static fix16_t b_full;
static fix16_t b_coil;
static fix16_t b_magnet;

static fix16_t x_sp_active;
static stream_id_t position_control_stream = VARS_STREAM_NONE;


/*
* In here do not substract the magnetic field caused by the current through the coil
*/
static void position_control_read_b(void)
{
    b_full_adc_raw = current_control_get_magnetic_field_adc_raw();
    b_full_valid = b_full_adc_raw <= POSITION_CONTROL_ADC_FULL_SCALE_COUNTS;
    if (!b_full_valid) {
        /* Preserve the last valid estimates; the stream carries the raw code and validity. */
        return;
    }

    const int32_t delta = (int32_t)b_full_adc_raw - POSITION_CONTROL_ADC_ZERO_FIELD_RAW;
    b_full = (fix16_t)(((int64_t)delta * fix16_one) / POSITION_CONTROL_ADC_COUNTS_PER_TESLA);

    // We use the setpoint since the current controller is very good and getting a live measurement here would introduce noise
    // We call setpoint_get_i_sp() to be able to calculate it even if the position controller is disabled
    const fix16_t i = setpoint_get_i_sp();

    b_coil = fix16_mul(fix16_mul(i, POSITION_CONTROL_COIL_T_PER_A), POSITION_CONTROL_ORIENTATION_CORRECTION);
    b_magnet = b_full - b_coil;

}


void position_control_init(void)
{
    position_control_stream = vars_create_stream("position", TASK_POSITION_CONTROL_HZ);

    VARS_MONITOR("b_full_adc_raw", VAR_U16, &b_full_adc_raw, position_control_stream);
    VARS_MONITOR("b_full_valid", VAR_U8, &b_full_valid, position_control_stream);
    VARS_MONITOR("b_full", VAR_F16, &b_full, position_control_stream);
    VARS_MONITOR("b_coil", VAR_F16, &b_coil, position_control_stream);
    VARS_MONITOR("b_magnet", VAR_F16, &b_magnet, position_control_stream);

    VARS_MONITOR("x_sp", VAR_F16, &x_sp_active, position_control_stream);

}

void position_control_task(void)
{
    const uint32_t tick = scheduler_get_tick();
    position_control_read_b();
    x_sp_active = setpoint_get_x_sp();
    vars_stream_emit_at(position_control_stream, tick);
}



// Returns the current setpoint calculated by the position controller
fix16_t position_control_get_i_sp(void){
    return 0;
}