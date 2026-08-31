#include "current_control.h"
#include "protocol.h"
#include "ch32fun.h"
#include "pinout.h"
#include "fsm.h"
#include "parameters.h"
#include "control_f16.h"
#include "tasks.h"

static pid_f16 current_controller = {0}; // Initialized in init_pins_current_control()

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
    TIM1->CTLR2 = TIM_MMS_0;   // TRGO on zero/reset event
    // TIM1->CTLR2 = TIM_MMS_1; // TRGO on update/ARR event

    /* Start the timer.
     * Once CEN is set, the PWM outputs begin toggling according to their compare values and polarity settings.
     * If this is missing, the timer will remain idle even though the pins are configured.
     */
    TIM1->CTLR1 |= TIM_CEN;

}

void init_adc_current_control(void){
    // Current reading synced to PWM with DMA
    // Voltage reading can be chill but it'd be nice if it was automatic too
}

void init_pins_current_control(void)
{
    init_pwm_current_control();
    init_adc_current_control();

    current_controller = (pid_f16){

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
