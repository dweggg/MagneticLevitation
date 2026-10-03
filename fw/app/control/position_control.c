#include "position_control.h"
#include "pinout.h"
#include "vars.h"
#include "current_control.h"
#include "scheduler.h"
#include "tasks.h"
#include "setpoint.h"

static fix16_t B_T;
static uint16_t magnetic_field_adc_raw;
static fix16_t x_sp_active;
static stream_id_t position_control_stream = VARS_STREAM_NONE;

static void position_control_update_magnetic_field(void)
{
    magnetic_field_adc_raw = current_control_get_magnetic_field_adc_raw();
    const int32_t delta = (int32_t)magnetic_field_adc_raw - POSITION_CONTROL_ADC_ZERO_FIELD_RAW;
    B_T = (fix16_t)((delta << 16) / POSITION_CONTROL_ADC_COUNTS_PER_TESLA);
}

void position_control_init(void)
{
    position_control_stream = vars_create_stream("position", TASK_POSITION_CONTROL_HZ);

    VARS_MONITOR("B_T", VAR_F16, &B_T, position_control_stream);
    VARS_MONITOR("magnetic_field_adc_raw", VAR_U16, &magnetic_field_adc_raw, position_control_stream);
    VARS_MONITOR("x_sp", VAR_F16, &x_sp_active, position_control_stream);

}

void position_control_task(void)
{
    const uint32_t tick = scheduler_get_tick();
    position_control_update_magnetic_field();
    x_sp_active = setpoint_get_x_sp();
    vars_stream_emit_at(position_control_stream, tick);
}


