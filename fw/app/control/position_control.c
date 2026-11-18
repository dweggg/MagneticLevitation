#include "position_control.h"
#include "pinout.h"
#include "vars.h"
#include "current_control.h"
#include "control_f16.h"
#include "scheduler.h"
#include "tasks.h"
#include "setpoint.h"

#define POSITION_CONTROL_COIL_HEIGHT_M F16(0.02f)
#define POSITION_CONTROL_NOMINAL_HEIGHT_M F16(0.05f)
#define POSITION_CONTROL_NOMINAL_FIELD_T F16(-0.0015f)
#define POSITION_CONTROL_MAX_HEIGHT_M F16(1.0f)
#define POSITION_CONTROL_CURRENT_LIMIT_A F16(4.5f)

static uint16_t b_full_adc_raw;
static uint8_t b_full_valid;
static fix16_t b_full;
static fix16_t b_coil;
static fix16_t b_magnet;

static fix16_t x_sp_active;
static fix16_t b_sp;
static fix16_t i_sp_position;
static pid_f16_t position_control_pid;
static stream_id_t position_control_stream = VARS_STREAM_NONE;

static void position_control_pid_reset(void)
{
    position_control_pid.integral_k = 0;
    position_control_pid.integral_k1 = 0;
    position_control_pid.e_k = 0;
    position_control_pid.e_k1 = 0;
    position_control_pid.out = 0;
}

static fix16_t position_control_get_b_sp(fix16_t x_sp)
{
    /* Approximate dipole field, referenced to the expected 50 mm hover height. */
    const fix16_t nominal_distance = POSITION_CONTROL_NOMINAL_HEIGHT_M + POSITION_CONTROL_COIL_HEIGHT_M;
    const fix16_t target_distance = x_sp + POSITION_CONTROL_COIL_HEIGHT_M;
    const fix16_t distance_ratio = fix16_div(nominal_distance, target_distance);
    const fix16_t ratio_squared = fix16_mul(distance_ratio, distance_ratio);
    const fix16_t ratio_cubed = fix16_mul(ratio_squared, distance_ratio);

    return fix16_mul(POSITION_CONTROL_NOMINAL_FIELD_T, ratio_cubed);
}


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
    VARS_MONITOR("b_sp", VAR_F16, &b_sp, position_control_stream);
    VARS_MONITOR("i_sp_position", VAR_F16, &i_sp_position, position_control_stream);

    /* Initial PI gains: output in amps for field error in tesla. */
    position_control_pid = (pid_f16_t){
        .kp = F16(50.0f),
        .ki = F16(10.0f),
        .kd = F16(10.0f),
        .ts = TASK_POSITION_CONTROL_PERIOD_S_Q16,
        .lim_p = POSITION_CONTROL_CURRENT_LIMIT_A,
        .lim_n = -POSITION_CONTROL_CURRENT_LIMIT_A,
    };

}

void position_control_task(void)
{
    const uint32_t tick = scheduler_get_tick();
    position_control_read_b();
    x_sp_active = setpoint_get_x_sp();

    if (!b_full_valid || x_sp_active <= 0 || x_sp_active > POSITION_CONTROL_MAX_HEIGHT_M) {
        position_control_pid_reset();
        b_sp = 0;
        i_sp_position = 0;
    } else {
        b_sp = position_control_get_b_sp(x_sp_active);
        position_control_pid.sp = b_sp;
        position_control_pid.fb = b_magnet;
        pid_f16_run(&position_control_pid);
        i_sp_position = position_control_pid.out;
    }

    vars_stream_emit_at(position_control_stream, tick);
}



// Returns the current setpoint calculated by the position controller
fix16_t position_control_get_i_sp(void){
    return i_sp_position;
}