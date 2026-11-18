#include "setpoint.h"
#include "current_control.h"
#include "position_control.h"
#include "vars.h"
#include "fsm.h"
#include "fault_limits.h"
#include "usb_pd.h"
#include "scheduler.h"
#include "tasks.h"

/*
 * NTC temperature conversion strategy:
 *  - ADC is 12-bit, so raw codes span 0..4095.
 *  - We do not compute the exponential thermistor equation on every sample.
 *  - Instead, we precompute the temperature curve in q16.16 fixed-point and
 *    interpolate between the nearest two table points.
 */
#define SETPOINT_NTC_ADC_BITS        12U
#define SETPOINT_NTC_ADC_MAX_RAW     ((1U << SETPOINT_NTC_ADC_BITS) - 1U)
#define SETPOINT_NTC_LUT_SHIFT       6U
#define SETPOINT_NTC_LUT_SIZE        (1U << SETPOINT_NTC_LUT_SHIFT)
#define SETPOINT_NTC_LUT_VALUE_COUNT (SETPOINT_NTC_LUT_SIZE + 1U)

/*
 * LUT layout:
 *  - Each entry is a temperature in q16.16 format.
 *  - Temperature values are stored as signed fix16_t, i.e. 1.0 == 0x00010000.
 *  - We sample the ADC range in 64-count chunks, so the table has 65 points:
 *    one at every 64-count boundary plus the final endpoint.
 *
 *  This was generated from a 10k/5.1k NTC divider curve. (https://www.lcsc.com/datasheet/C49247665.pdf)
 */
static const fix16_t setpoint_ntc_temp_c_q16_lut[SETPOINT_NTC_LUT_VALUE_COUNT] = {
    F16(-40.0), F16(-40.0), F16(-32.42), F16(-25.12), 
    F16(-19.58), F16(-15.02), F16(-11.11), F16(-7.66), 
    F16(-4.53), F16(-1.66), F16(1.00), F16(3.51), 
    F16(5.88), F16(8.14), F16(10.31), F16(12.41), 
    F16(14.43), F16(16.41), F16(18.33), F16(20.22), 
    F16(22.07), F16(23.89), F16(25.69), F16(27.48), 
    F16(29.25), F16(31.02), F16(32.77), F16(34.53), 
    F16(36.29), F16(38.06), F16(39.84), F16(41.62), 
    F16(43.43), F16(45.23), F16(47.08), F16(48.96), 
    F16(50.88), F16(52.83), F16(54.83), F16(56.87), 
    F16(58.97), F16(61.14), F16(63.37), F16(65.69), 
    F16(68.09), F16(70.60), F16(73.23), F16(75.99), 
    F16(78.90), F16(81.98), F16(85.27), F16(88.80), 
    F16(92.62), F16(96.78), F16(101.36), F16(106.46), 
    F16(112.22), F16(118.84), F16(126.63), F16(136.09), 
    F16(148.08), F16(150.0), F16(150.0), F16(150.0),
    F16(150.0)
};

static fix16_t setpoint_temp_c_q16_from_adc_raw(uint16_t adc_raw)
{
    uint16_t index;
    uint16_t fract;
    fix16_t temp_lo;
    fix16_t temp_hi;

    /* Clamp the raw ADC value to the last legal table point. */
    if (adc_raw >= SETPOINT_NTC_ADC_MAX_RAW) {
        adc_raw = SETPOINT_NTC_ADC_MAX_RAW;
    }

    /*
     * Divide the 12-bit ADC code into 64-count segments.
     *  - index selects the LUT entry below the current raw value.
     *  - fract is the remainder inside that segment, i.e. how far between the
     *    lower and upper LUT points the raw value sits.
     */
    index = adc_raw >> SETPOINT_NTC_LUT_SHIFT;
    fract = adc_raw & ((1U << SETPOINT_NTC_LUT_SHIFT) - 1U);

    /* Guard against walking exactly to the final table index. */
    if (index >= SETPOINT_NTC_LUT_SIZE) {
        index = SETPOINT_NTC_LUT_SIZE - 1U;
    }

    /*
     * Linear interpolation (LERP):
     *   temp = temp_lo + (temp_hi - temp_lo) * f
     *
     * Here, temp_lo and temp_hi are the nearest two calibrated temperatures in
     * the LUT, and f is how far we are between them inside this 64-count ADC
     * segment.
     *
     * Example:
     *   raw = 1200
     *   index = 1200 >> 6 = 18
     *   fract = 1200 & 63 = 48
     *   => we are 48/64 = 0.75 of the way between LUT[18] and LUT[19]
     *
     * The q16.16 fixed-point version of f is built from the 6-bit remainder by
     * expanding it into the 16-bit fraction domain:
     *   f_q16 = fract << (16 - 6) = fract << 10
     *
     * This turns a value like 48 into roughly 49152, which corresponds to 0.75
     * in q16.16. Then fix16_lerp16 does exactly:
     *   temp = temp_lo + (temp_hi - temp_lo) * f_q16
     */
    temp_lo = setpoint_ntc_temp_c_q16_lut[index];
    temp_hi = setpoint_ntc_temp_c_q16_lut[index + 1U];
    return fix16_lerp16(temp_lo, temp_hi, (uint16_t)((uint32_t)fract << (16U - SETPOINT_NTC_LUT_SHIFT)));
}


static fix16_t temp_c_q16;
static fix16_t i_sp_manual;
static fix16_t i_sp_position_control;
static fix16_t x_sp_manual;
static fix16_t i_sp_active;
static fix16_t x_sp_active;
static fix16_t power_budget_w;
static uint16_t power_budget_pct = 100U; // uint16_t because in the next rev the PCB we will have a potentiometer connected to an ADC channel
static uint8_t power_available_w;
static stream_id_t setpoint_stream = VARS_STREAM_NONE;

void setpoint_init(void)
{
    
    setpoint_stream = vars_create_stream("setpoint", TASK_SETPOINT_HZ);

    VARS_MONITOR("temp_c", VAR_F16, &temp_c_q16, setpoint_stream);
    VARS_PARAM("power_budget_pct", VAR_U16, &power_budget_pct);
    VARS_PARAM("i_sp_manual", VAR_F16, &i_sp_manual);
    VARS_PARAM("x_sp_manual", VAR_F16, &x_sp_manual);
    VARS_MONITOR("power_available_w", VAR_U8, &power_available_w, setpoint_stream);
    VARS_MONITOR("power_budget_w", VAR_F16, &power_budget_w, setpoint_stream);
    VARS_MONITOR("i_sp_active", VAR_F16, &i_sp_active, setpoint_stream);
    VARS_MONITOR("x_sp_active", VAR_F16, &x_sp_active, setpoint_stream);
}

void setpoint_task(void)
{

    const uint32_t tick = scheduler_get_tick();

    const uint16_t temp_adc_raw = current_control_get_temp_adc_raw();
    temp_c_q16 = setpoint_temp_c_q16_from_adc_raw(temp_adc_raw);

    if (temp_c_q16 >= FAULT_OVERTEMPERATURE_LIMIT_C_Q16 || temp_c_q16 < 0) {
        fsm_raise_fault(FAULT_OVERTEMPERATURE);
    }

    power_available_w = usb_pd_get_available_power_w();
    if (power_budget_pct > 100U) {
        power_budget_pct = 100U;
    }
    power_budget_w = fix16_mul(
        fix16_from_int(power_available_w),
        fix16_div(fix16_from_int(power_budget_pct), fix16_from_int(100))
    );

    i_sp_position_control = position_control_get_i_sp();
    
    i_sp_active = (fsm_control_mode() == FSM_MODE_MANUAL_CURRENT)
        ? i_sp_manual : i_sp_position_control; // either from comms or position control sets it
    x_sp_active = (fsm_control_mode() == FSM_MODE_MANUAL_POSITION)
        ? x_sp_manual : 0; // TODO: calculate from available power and magnet/weight limit

    vars_stream_emit_at(setpoint_stream, tick);
}

fix16_t setpoint_get_i_sp(void)
{
    return i_sp_active;
}

fix16_t setpoint_get_x_sp(void)
{
    return x_sp_active;
}

fix16_t setpoint_get_power_budget_w(void)
{
    return power_budget_w;
}

fix16_t setpoint_get_temp_c(void)
{
    return temp_c_q16;
}
