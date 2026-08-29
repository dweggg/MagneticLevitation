#include "current_control.h"
#include "protocol.h"
#include "ch32fun.h"
#include "pinout.h"

void init_pins_current_control(void)
{
    /* Timer clock is the MCU system clock. Changing this or the prescaler changes the PWM base frequency. */
    const uint32_t timer_clk_hz = FUNCONF_SYSTEM_CORE_CLOCK;

    /* PWM period in timer ticks: f_pwm = timer_clk_hz / (PSC + 1) / (ARR + 1).
     * Here PSC = 0, so the PWM frequency is simply timer_clk_hz / (ARR + 1).
     * Raising PWM_SWITCHING_FREQUENCY_HZ increases the timer rate and reduces ARR, so the bridge switches faster.
     */
    const uint32_t pwm_period_ticks = (timer_clk_hz / PWM_SWITCHING_FREQUENCY_HZ) - 1U;

    /* Convert deadtime from nanoseconds to timer ticks.
     * This is a coarse approximation and is mainly used for the BDTR dead-time generator.
     * Use 64-bit arithmetic here: 48 MHz * 200 ns = 9.6e9, which exceeds 32-bit range and wraps
     * if kept in uint32_t, which is exactly why the deadtime calculation was incorrectly landing at 1.
     */
    const uint64_t deadtime_ticks_raw = ((uint64_t)timer_clk_hz * (uint64_t)PWM_DEADTIME_NS + 500000000ULL) / 1000000000ULL;
    uint32_t deadtime_ticks = (uint32_t)deadtime_ticks_raw;

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

    /* Auto-reload register sets the PWM period in timer ticks.
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

    /* ARPE enables preload on ARR, delaying updates until the next update event.
     * This keeps the PWM frequency stable when changing duty cycles mid-run.
     */
    TIM1->CTLR1 = TIM_ARPE;

    /* Start the timer.
     * Once CEN is set, the PWM outputs begin toggling according to their compare values and polarity settings.
     * If this is missing, the timer will remain idle even though the pins are configured.
     */
    TIM1->CTLR1 |= TIM_CEN;
}

void task_current_control(void)
{
    /* Safe default: keep the bridge disabled until the control loop updates duty cycles. */
    TIM1->CH1CVR = 0;
    TIM1->CH2CVR = 0;
}