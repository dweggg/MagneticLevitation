#include "current_control.h"
#include "protocol.h"
#include "ch32fun.h"
#include "pinout.h"
#include "fsm.h"
#include "parameters.h"
#include "control_f16.h"
#include "tasks.h"

static pid_f16_t current_controller = {0}; // Initialized in init_pins_current_control()

#define ADC_SEQUENCE_LENGTH 5U
#define ADC_DMA_FRAME_COUNT 32U
#define ADC_DMA_SAMPLE_COUNT (ADC_SEQUENCE_LENGTH * ADC_DMA_FRAME_COUNT)

static volatile uint16_t adc_dma_buffer[ADC_DMA_SAMPLE_COUNT];
static uint16_t adc_dma_read_frame;
static uint16_t adc_mag_raw;
static uint16_t adc_temp_raw;

static fix16_t v_meas;
static fix16_t i_fb;

static void init_adc_current_control(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR |= RCC_ADC1RST;
    RCC->APB2PRSTR &= ~RCC_ADC1RST;

    /* Keep the ADC clock at 6 MHz and use one scan frame per TIM1 update. */
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV8;
    
    /*
    * ADC sample-time codes (SAMPTR field, 3 bits, values 0-7).
    * Table is fixed by hardware — this doesn't change with your settings below,
    * it's just what each code means. ADC clock here is 6 MHz.
    *
    *   code | ADC_SMPx bits set          | sample cycles | sample time @ 6 MHz
    *   -----|-----------------------------|---------------|---------------------
    *     0  | (none)                      |      3        |   0.50 us
    *     1  | SMP0_0                      |      9        |   1.50 us
    *     2  | SMP0_1                      |     15        |   2.50 us
    *     3  | SMP0_0 | SMP0_1             |     30        |   5.00 us
    *     4  | SMP0_2                      |     43        |   7.17 us
    *     5  | SMP0_2 | SMP0_0             |     57        |   9.50 us
    *     6  | SMP0_2 | SMP0_1             |     73        |  12.17 us
    *     7  | SMP0_2 | SMP0_1 | SMP0_0    |    241        |  40.17 us
    */
    ADC1->SAMPTR2 = ((ADC_SMP0_1)  << (3U * 2U)) |                              // MAG   
                    ((ADC_SMP0_1)  << (3U * 3U)) |                              // V_MEAS
                    ((ADC_SMP0_0 | ADC_SMP0_2)  << (3U * 4U)) |                 // I_REF 
                    ((ADC_SMP0_1 | ADC_SMP0_2)  << (3U * 5U)) |                 // I_MEAS
                    ((ADC_SMP0_1)  << (3U * 6U));                               // TEMP  
    ADC1->SAMPTR1 = 0;
    ADC1->CTLR1 = ADC_SCAN;
    ADC1->RSQR1 = ((ADC_SEQUENCE_LENGTH - 1U) << 20U);
    ADC1->RSQR2 = 0;
    ADC1->RSQR3 = (3U << 0U) | (4U << 5U) | (5U << 10U) |
                  (2U << 15U) | (6U << 20U);
    ADC1->CTLR2 = ADC_ADON;

    ADC1->CTLR2 |= CTLR2_RSTCAL_Set;
    while (ADC1->CTLR2 & CTLR2_RSTCAL_Set) {}
    ADC1->CTLR2 |= CTLR2_CAL_Set;
    while (ADC1->CTLR2 & CTLR2_CAL_Set) {}

    RCC->AHBPCENR |= RCC_DMA1EN;
    DMA1_Channel1->CFGR = 0;
    DMA1_Channel1->PADDR = (uintptr_t)&ADC1->RDATAR;
    DMA1_Channel1->MADDR = (uintptr_t)adc_dma_buffer;
    DMA1_Channel1->CNTR = ADC_DMA_SAMPLE_COUNT;
    DMA1_Channel1->CFGR = DMA_CFGR1_CIRC | DMA_CFGR1_MINC |
                          DMA_CFGR1_PSIZE_0 | DMA_CFGR1_MSIZE_0 |
                          DMA_CFGR1_PL_1 | DMA_CFGR1_EN;

    /* EXTSEL=0 is TIM1_TRGO on CH32X035; TIM1 update is the trigger. */
    ADC1->CTLR2 |= ADC_DMA | ADC_EXTTRIG;
}

static void drain_adc_dma(void)
{
    uint32_t voltage_sum = 0;
    uint32_t current_ref_sum = 0;
    uint32_t current_meas_sum = 0;
    uint32_t mag_sum = 0;
    uint32_t temp_sum = 0;
    uint16_t frame_count = 0;
    const uint16_t write_sample =
        (uint16_t)((ADC_DMA_SAMPLE_COUNT - DMA1_Channel1->CNTR) % ADC_DMA_SAMPLE_COUNT);
    const uint16_t write_frame = (uint16_t)(write_sample / ADC_SEQUENCE_LENGTH);

    while (adc_dma_read_frame != write_frame && frame_count < ADC_DMA_FRAME_COUNT) {
        const uint16_t offset = adc_dma_read_frame * ADC_SEQUENCE_LENGTH;
        voltage_sum += adc_dma_buffer[offset + 0U];
        current_ref_sum += adc_dma_buffer[offset + 1U];
        current_meas_sum += adc_dma_buffer[offset + 2U];
        mag_sum += adc_dma_buffer[offset + 3U];
        temp_sum += adc_dma_buffer[offset + 4U];
        adc_dma_read_frame = (uint16_t)((adc_dma_read_frame + 1U) % ADC_DMA_FRAME_COUNT);
        ++frame_count;
    }

    if (frame_count == 0U) {
        return;
    }

    const uint16_t voltage_raw = (uint16_t)(voltage_sum / frame_count);
    const uint16_t current_ref_raw = (uint16_t)(current_ref_sum / frame_count);
    const uint16_t current_meas_raw = (uint16_t)(current_meas_sum / frame_count);
    adc_mag_raw = (uint16_t)(mag_sum / frame_count);
    adc_temp_raw = (uint16_t)(temp_sum / frame_count);

    v_meas = fix16_mul(fix16_from_int(voltage_raw), ADC_V_MEAS_GAIN);
    i_fb = fix16_mul(fix16_from_int((int32_t)current_meas_raw - (int32_t)current_ref_raw), ADC_I_MEAS_GAIN);

    parameters_publish(PARAM_ID_V_MEAS, &v_meas);
    parameters_publish(PARAM_ID_I_REF, &current_ref_raw);
    parameters_publish(PARAM_ID_I_MEAS, &current_meas_raw);
    parameters_publish(PARAM_ID_I_FB, &i_fb);
}

/* Convert a Q16.16 duty ratio into timer counts. We keep the helper here so the
 * USB parameter writes can feed it directly without any extra scaling code in the
 * task loop.
 */
uint16_t current_control_duty_to_ticks(fix16_t duty_q16)
{
    const uint16_t period_ticks = TIM1->ATRLR;

    if (period_ticks == 0U) {
        return 0U;
    }

    uint32_t tick_count = (uint32_t)fix16_to_int(
        fix16_mul(
            fix16_clamp(duty_q16, 0, fix16_one),
            fix16_from_int((int32_t)period_ticks)
        )
    );

    if (tick_count > period_ticks) {
        tick_count = period_ticks;
    }

    return (uint16_t)tick_count;
}

void init_pwm_current_control(void){
        /* Timer clock is the MCU system clock. Changing this or the prescaler changes the PWM base frequency. */
    const uint32_t timer_clk_hz = FUNCONF_SYSTEM_CORE_CLOCK;

    /* Center-aligned PWM traverses ARR on the way up and down, so its frequency is:
     * f_pwm = timer_clk_hz / ((PSC + 1) * 2 * (ARR + 1)).
     */
    const uint32_t pwm_period_ticks =
        (timer_clk_hz / (2U * PWM_SWITCHING_FREQUENCY_HZ)) - 1U;

    /* Convert deadtime from nanoseconds to timer ticks.
     * This is a coarse approximation and is mainly used for the BDTR dead-time generator.
     * Use 64-bit arithmetic here: 48 MHz * 200 ns = 9.6e9, which exceeds 32-bit range and wraps
     * if kept in uint32_t, which is exactly why the deadtime calculation was incorrectly landing at 1.
     */
    const uint64_t deadtime_ticks_raw = ((uint64_t)timer_clk_hz * (uint64_t)PWM_DEADTIME_NS + 500000000ULL) / 1000000000ULL;
    uint32_t deadtime_ticks = (uint32_t)deadtime_ticks_raw;

    /* Keep the startup values visible in the USB log so we can tune the timer without extra tooling. */
    LOG("period_ticks=%u deadtime_ticks=%u", (unsigned int)pwm_period_ticks, (unsigned int)deadtime_ticks);

    /* Enable TIM1 clock in APB2 bus domain. Without this, the peripheral is held in reset/disabled
     * and all TIM1 registers read back as zero even though the code writes to them.
     */
    RCC->APB2PCENR |= RCC_APB2Periph_TIM1;

    /* Configure each PWM pin as a 50 MHz alternate-function push-pull output.
     * This is the correct GPIO mode for TIMER-driven bridge outputs on CH32.
     * Using a lower drive speed or wrong GPIO mode will make the PWM output behave unpredictably or not toggle at all.
     */
    funPinMode(PIN_PWM_AL, GPIO_CFGLR_OUT_50Mhz_AF_PP);
    funPinMode(PIN_PWM_BL, GPIO_CFGLR_OUT_50Mhz_AF_PP);
    funPinMode(PIN_PWM_AH, GPIO_CFGLR_OUT_50Mhz_AF_PP);
    funPinMode(PIN_PWM_BH, GPIO_CFGLR_OUT_50Mhz_AF_PP);

    /* Clamp dead time to the 8-bit DTG field used by the timer BDTR register.
     * If the requested dead time is larger than the hardware field supports, the timer saturates at the maximum value.
     */
    if (deadtime_ticks > 0xFFU) {
        deadtime_ticks = 0xFFU;
    }

    /* No prescaler: timer runs at the source clock directly.
     * Changing PSC changes the effective timer frequency without changing the ARR formula directly.
     */
    TIM1->PSC = 0;

    /* Auto-reload register sets the half-cycle limit in timer ticks.
     * Higher ARR => lower PWM frequency. Lower ARR => higher PWM frequency.
     * This is the main frequency knob for the bridge.
     */
    TIM1->ATRLR = pwm_period_ticks;

    /* Repetition counter is left at zero for standard PWM behavior.
     * A nonzero REP value adds extra update events per cycle and is usually relevant for advanced dead-time or ADC sync schemes.
     */
    TIM1->RPTCR = 0;

    /* Reset the counter and clear the compare values before starting the timer.
     * This avoids random duty pulses during startup.
     */
    TIM1->CNT = 0;
    TIM1->CH1CVR = 0;
    TIM1->CH2CVR = 0;

    /* CH1 and CH2 are configured for PWM mode 1 on the main outputs.
     * The low-side complementary outputs are enabled in the CCER register below.
     * PWM mode 1 means the channel is active while CNT < CCR, and inactive otherwise.
     * Changing this setting to a different mode changes the output polarity and expected duty-cycle behavior.
     */
    TIM1->CHCTLR1 = (TIM_OC1M_1 | TIM_OC1M_2) | (TIM_OC2M_1 | TIM_OC2M_2);

    /* CH3 / CH4 are unused here; keeping CHCTLR2 at zero means they stay in the default disabled capture/compare state. */
    TIM1->CHCTLR2 = 0;

    /* Enable the main and complementary output stages for the two half-bridges.
     * CC1E / CC2E = CH1 / CH2 normal outputs enabled.
     * CC1NE / CC2NE = CH1 / CH2 complementary outputs enabled.
     * These correspond to the PB9/PB10 high-side and PB6/PB7 low-side pins.
     */
    TIM1->CCER = TIM_CC1E | TIM_CC2E | TIM_CC1NE | TIM_CC2NE;

    /* Per-channel polarity is controlled here.
     * Setting a bit flips the active state of that output relative to the PWM compare logic.
     * For example: 1 = invert the channel output polarity, 0 = active-high / normal polarity.
     */
    TIM1->CCER |= (PWM_AH_POLARITY ? TIM_CC1P : 0U);
    TIM1->CCER |= (PWM_AL_POLARITY ? TIM_CC1NP : 0U);
    TIM1->CCER |= (PWM_BH_POLARITY ? TIM_CC2P : 0U);
    TIM1->CCER |= (PWM_BL_POLARITY ? TIM_CC2NP : 0U);

    /* Break and dead-time register setup.
     * TIM_MOE = main output enable; without this, the timer can be clocked but the bridge output stage stays off.
     * TIM_AOE = automatic output enable; allows the output to re-enable after faults if configured elsewhere.
     * DTG is the dead-time generator field, which inserts a delay between complementary transitions to prevent shoot-through.
     * Increasing deadtime reduces cross-conduction risk but also reduces usable duty range near the rails.
     */
    TIM1->BDTR = TIM_MOE | TIM_AOE | (deadtime_ticks & TIM_DTG);

    /* Generate an update event to load the shadow registers.
     * This ensures ARR, PSC, and PWM mode changes take effect before the timer is started.
     */
    TIM1->SWEVGR = TIM_UG;

    /* Center-aligned up/down counter mode matches the STM32 style centered PWM pattern.
     * The timer counts up to ARR and then back down, which is convenient for half-bridge drive.
     */
    TIM1->CTLR1 = TIM_ARPE | TIM_CMS_1;

    /* TRGO is left selectable here: the 0-event and ARR-event are both valid startup trials.
     * Try one and comment the other out as needed to find the cleanest trigger behavior.
     */
    //TIM1->CTLR2 = TIM_MMS_0;   // TRGO on zero/reset event
    TIM1->CTLR2 = TIM_MMS_1; // TRGO on update/ARR event

    /* Start the timer.
     * Once CEN is set, the PWM outputs begin toggling according to their compare values and polarity settings.
     * If this is missing, the timer will remain idle even though the pins are configured.
     */
    TIM1->CTLR1 |= TIM_CEN;

}

void init_pins_current_control(void)
{
    funPinMode(PIN_V_MEAS, GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_I_REF, GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_I_MEAS, GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_MAG_MEAS, GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_TEMP_MEAS, GPIO_CFGLR_IN_ANALOG);
    init_adc_current_control();
    init_pwm_current_control();

    current_controller = (pid_f16_t){

        // PI tuned with load parameters aiming for a ~1 ms step response time which equates to roughly 300 rad/s
        .kp = fix16_from_float(0.9f), // bw[rad/s] * L[H] = 300 rad/s * 3 mH = 0.9
        .ki = fix16_from_float(600.0f), // bw[rad/s] * R[Ohm] = 300 rad/s * 2 Ohm = 600

        .kd = fix16_from_float(0.0f), // PI, no D

        .ts = TASK_CURRENT_CONTROL_S_F16,
        .lim_p = fix16_from_float(1.0f), // maximum output voltage to be updated with VBUS measurement
        .lim_n = fix16_from_float(0.0f), // minimum output voltage
    };
}


void task_current_control(void)
{
    drain_adc_dma();

    /* The bridge is only allowed to output PWM when the FSM says the control loop is active */
    if (fsm_state() != FSM_CURRENT_CONTROL) {
        TIM1->CH1CVR = 0;
        TIM1->CH2CVR = 0;
        return;
    }

    // We input raw duty during bring up
    fix16_t duty_a = 0;
    fix16_t duty_b = 0;

    if (parameters_fetch(PARAM_ID_DUTY_A, &duty_a, sizeof(duty_a)) < 0) {
        duty_a = 0;
    }
    if (parameters_fetch(PARAM_ID_DUTY_B, &duty_b, sizeof(duty_b)) < 0) {
        duty_b = 0;
    }

    if (TIM1->CH1CVR != current_control_duty_to_ticks(duty_a)) {
        TIM1->CH1CVR = current_control_duty_to_ticks(duty_a);
        LOG("ticks_a=%u ticks_b=%u", (unsigned int)TIM1->CH1CVR, (unsigned int)TIM1->CH2CVR);

    }
    if (TIM1->CH2CVR != current_control_duty_to_ticks(duty_b)) {
        TIM1->CH2CVR = current_control_duty_to_ticks(duty_b);
        LOG("ticks_a=%u ticks_b=%u", (unsigned int)TIM1->CH1CVR, (unsigned int)TIM1->CH2CVR);
    }

    return;

    // /* Current loop */
    // fix16_t i_sp = 0; //TODO: current setpoint, to be fetched from position_control

    // fix16_t i_fb = 0; //TODO: current measurement, to be updated with ADC
    // fix16_t v_lim = fix16_from_float(20.0f); //TODO: voltage limit, to be updated with ADC
    
    // fix16_t mi = 0; // modulation index, -1..1

    // current_controller.sp = i_sp;
    // current_controller.fb = i_fb;

    // current_controller.lim_p = v_lim; //voltage limit

    // pid_f16_run(&current_controller);
    
    // mi = fix16_div(current_controller.out, v_lim);

    // // duty A is mi/2 + 0.5, since duty 0.5 is 0V, duty 0 is -v_lim and duty 1 is +v_lim
    // // duty B is just 1-duty_a
    // duty_a = fix16_mul(mi, fix16_from_float(0.5f)) + fix16_from_float(0.5f);
    // duty_b = fix16_from_float(1.0f) - duty_a;

    // TIM1->CH1CVR = current_control_duty_to_ticks(duty_a);
    // TIM1->CH2CVR = current_control_duty_to_ticks(duty_b);

}

uint16_t get_mag_meas_raw(void)
{
    return adc_mag_raw;
}

uint16_t get_temp_meas_raw(void)
{
    return adc_temp_raw;
}
