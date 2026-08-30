#include "setpoint.h"
#include "protocol.h"
#include "parameters.h"
#include "ch32fun.h"
#include "pinout.h"
#include "fix16.h"

/*
 * NTC temperature conversion strategy:
 *  - ADC is 12-bit, so raw codes span 0..4095.
 *  - We do not compute the exponential thermistor equation on every sample.
 *  - Instead, we precompute the temperature curve in q16.16 fixed-point and
 *    interpolate between the nearest two table points.
 */
#define NTC_ADC_BITS              12U
#define NTC_ADC_MAX_RAW           ((1U << NTC_ADC_BITS) - 1U)
#define NTC_LUT_SHIFT             6U
#define NTC_LUT_SIZE              (1U << NTC_LUT_SHIFT)
#define NTC_LUT_VALUE_COUNT       (NTC_LUT_SIZE + 1U)

/*
 * LUT layout:
 *  - Each entry is a temperature in q16.16 format.
 *  - Temperature values are stored as signed fix16_t, i.e. 1.0 == 0x00010000.
 *  - We sample the ADC range in 64-count chunks, so the table has 65 points:
 *    one at every 64-count boundary plus the final endpoint.
 *
 *  This was generated from a 10k/5.1k NTC divider curve and is the
 *  canonical temperature model used by the firmware.
 */
static const fix16_t ntc_lut_q16[NTC_LUT_VALUE_COUNT] = {
    -2621440, -2621440, -2125050, -1646743, -1283307, -984969, -728754, -502058,
    -297181, -109051, 65864, 230127, 385667, 533975, 676232, 813396,
    946255, 1075470, 1201605, 1325147, 1446522, 1566109, 1684251, 1801259,
    1917420, 2033004, 2148268, 2263457, 2378811, 2494565, 2610956, 2728220,
    2846601, 2964467, 3085820, 3209077, 3334532, 3462505, 3593339, 3727415,
    3865149, 4007009, 4153519, 4305275, 4462958, 4627353, 4799377, 4980110,
    5170835, 5373100, 5588791, 5820246, 6070405, 6343038, 6643087, 6977201,
    7354603, 7788589, 8299274, 8919060, 9704832, 9830400, 9830400, 9830400,
    9830400
};

static fix16_t temp_c_q16_from_raw(uint16_t raw)
{
    uint16_t index;
    uint16_t fract;
    fix16_t temp_lo;
    fix16_t temp_hi;

    /* Clamp the raw ADC value to the last legal table point. */
    if (raw >= NTC_ADC_MAX_RAW) {
        raw = NTC_ADC_MAX_RAW;
    }

    /*
     * Divide the 12-bit ADC code into 64-count segments.
     *  - index selects the LUT entry below the current raw value.
     *  - fract is the remainder inside that segment, i.e. how far between the
     *    lower and upper LUT points the raw value sits.
     */
    index = raw >> NTC_LUT_SHIFT;
    fract = raw & ((1U << NTC_LUT_SHIFT) - 1U);

    /* Guard against walking exactly to the final table index. */
    if (index >= (NTC_LUT_SIZE)) {
        index = NTC_LUT_SIZE - 1U;
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
    temp_lo = ntc_lut_q16[index];
    temp_hi = ntc_lut_q16[index + 1U];
    return fix16_lerp16(temp_lo, temp_hi, (uint16_t)((uint32_t)fract << (16U - NTC_LUT_SHIFT)));
}


static volatile fix16_t temp_c_q16;

void init_pins_setpoint(void){
    funPinMode(PIN_TEMP_MEAS, GPIO_CFGLR_IN_ANALOG);
    funAnalogInit();
}

void task_setpoint(void){

    const uint16_t raw = (uint16_t)funAnalogRead(PIN_TEMP_MEAS);
    parameters_publish_u16(PARAM_ID_TEMP_RAW, raw);
    
    temp_c_q16 = temp_c_q16_from_raw(raw);

    LOG("TEMP (degC*10): %u", (unsigned int)fix16_to_int(fix16_mul(temp_c_q16, fix16_from_int(10))));
}
