#include "current_control.h"
#include "protocol.h"
#include "ch32fun.h"
#include "pinout.h"
#include "fsm.h"
#include "parameters.h"
#include "control_f16.h"
#include "tasks.h"


/* ============================================================================
 * ADC Configuration
 * ========================================================================== */

#define ADC_SEQUENCE_LENGTH    5U
#define ADC_DMA_FRAME_COUNT    32U
#define ADC_DMA_SAMPLE_COUNT   (ADC_SEQUENCE_LENGTH * ADC_DMA_FRAME_COUNT)


/* ============================================================================
 * Module state
 * ========================================================================== */

/* ADC DMA buffer:
 *
 * Each frame contains:
 *   [0] V_MEAS
 *   [1] I_REF
 *   [2] I_MEAS
 *   [3] MAG
 *   [4] TEMP
 */
static volatile uint16_t adc_dma_buffer[ADC_DMA_SAMPLE_COUNT];
static uint16_t adc_dma_read_frame;

static uint16_t adc_mag_raw;
static uint16_t adc_temp_raw;

static fix16_t v_meas;
static fix16_t i_fb;


/* Current controller.
 * Gains and limits are configured in init_current_controller().
 */
static pid_f16_t current_controller = {0};
static fix16_t current_kp;
static fix16_t current_ki;
static uint32_t pwm_switching_frequency_hz;

/* ============================================================================
 * Forward declarations
 * ========================================================================== */

static void init_adc_current_control(void);
static void drain_adc_dma(void);
static void init_current_controller(void);
static void update_current_controller_parameters(void);
static void apply_pwm_switching_frequency(uint32_t frequency_hz);

/* ============================================================================
 * Control loop
 *
 * This is the main entry point for the current-control task.
 *
 * Current bring-up behavior:
 *   1. Process the latest ADC samples.
 *   2. Make sure the FSM allows PWM output.
 *   3. Fetch raw duty-cycle commands.
 *   4. Apply the duty cycles to TIM1.
 *
 * TODO:
 *   Replace the raw duty commands with the closed-loop PI controller.
 * ========================================================================== */

void task_current_control(void)
{
    /*
     * Always process ADC data, even when PWM output is disabled.
     */
    drain_adc_dma();

    /* Fetch parameter updates from comms */
    update_current_controller_parameters();

    /*
     * PWM is only allowed while the FSM is in the current-control state.
     * Any other state forces both bridge outputs off.
     */
    if (fsm_state() != FSM_CURRENT_CONTROL) {
        TIM1->CH1CVR = 0;
        TIM1->CH2CVR = 0;
        return;
    }

    /*
     * For now we accept raw duty-cycle commands from the parameter system.
     * duty_a and duty_b are Q16.16 values in the range [0.0, 1.0].
     */
    fix16_t duty_a = 0;
    fix16_t duty_b = 0;

    if (parameters_fetch(PARAM_ID_DUTY_A, &duty_a, sizeof(duty_a)) < 0) {
        duty_a = 0;
    }

    if (parameters_fetch(PARAM_ID_DUTY_B, &duty_b, sizeof(duty_b)) < 0) {
        duty_b = 0;
    }

    const uint16_t ticks_a = current_control_duty_to_ticks(duty_a);
    const uint16_t ticks_b = current_control_duty_to_ticks(duty_b);

    /*
     * Only write the timer registers when the duty actually changes.
     */
    if (TIM1->CH1CVR != ticks_a || TIM1->CH2CVR != ticks_b) {
        TIM1->CH1CVR = ticks_a;
        TIM1->CH2CVR = ticks_b;

        LOG("ticks_a=%u ticks_b=%u",
            (unsigned int)ticks_a,
            (unsigned int)ticks_b);
    }


    /*
     * ------------------------------------------------------------------------
     * Closed-loop current control (TODO)
     * ------------------------------------------------------------------------
     *
     *     fix16_t i_sp = 0;       // TODO: fetch current setpoint
     *     fix16_t modulation_index = 0;
     *
     *     current_controller.sp = i_sp;
     *     current_controller.fb = i_fb;
     *     current_controller.lim_p = v_meas;
     *     current_controller.lim_n = -v_meas;
     *
     *     pid_f16_run(&current_controller);
     *
     *     modulation_index = fix16_div(current_controller.out, v_meas);
     *
     *     // duty = 0.5 gives zero output voltage.
     *     // duty = 0.0 gives -v_meas.
     *     // duty = 1.0 gives +v_meas.
     *     duty_a = fix16_mul(modulation_index, fix16_from_float(0.5f))
     *            + fix16_from_float(0.5f);
     *     duty_b = fix16_one - duty_a;
     *
     *     TIM1->CH1CVR = current_control_duty_to_ticks(duty_a);
     *     TIM1->CH2CVR = current_control_duty_to_ticks(duty_b);
     */
}


/* ============================================================================
 * Current-controller configuration
 * ========================================================================== */

static void init_current_controller(void)
{
    /*
     * PI tuning:
     *
     * Target bandwidth ~ 300 rad/s
     *
     *     Kp = bandwidth * L
     *        = 300 rad/s * 3 mH
     *        = 0.9
     *
     *     Ki = bandwidth * R
     *        = 300 rad/s * 2 Ohm
     *        = 600
     *
     * No derivative term is used.
     */
    current_controller = (pid_f16_t){
        .kp = fix16_from_float(0.9f),
        .ki = fix16_from_float(600.0f),
        .kd = fix16_from_float(0.0f),

        .ts = TASK_CURRENT_CONTROL_S_F16,

        /* Output is currently limited to the available voltage range. */
        .lim_p = fix16_from_float(1.0f),
        .lim_n = fix16_from_float(0.0f),
    };
}

static void update_current_controller_parameters(void)
{
    fix16_t kp;
    fix16_t ki;
    uint32_t switching_frequency_hz;

    if (parameters_fetch(
            PARAM_ID_CURRENT_KP,
            &kp,
            sizeof(kp)) >= 0) {

        if (kp != current_kp) {
            current_kp = kp;
            current_controller.kp = kp;
        }
    }

    if (parameters_fetch(
            PARAM_ID_CURRENT_KI,
            &ki,
            sizeof(ki)) >= 0) {

        if (ki != current_ki) {
            current_ki = ki;
            current_controller.ki = ki;
        }
    }

    if (parameters_fetch(
            PARAM_ID_PWM_SWITCHING_FREQUENCY,
            &switching_frequency_hz,
            sizeof(switching_frequency_hz)) >= 0) {
        
        if (switching_frequency_hz != pwm_switching_frequency_hz) {
            apply_pwm_switching_frequency(
                switching_frequency_hz
            );

            pwm_switching_frequency_hz =
                switching_frequency_hz;

            LOG("PWM switching frequency updated to %u Hz", (unsigned int)pwm_switching_frequency_hz);
        }
    }
}


/* ============================================================================
 * ADC
 * ========================================================================== */

static void init_adc_current_control(void)
{
    /* Enable and reset ADC1. */
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;

    RCC->APB2PRSTR |= RCC_ADC1RST;
    RCC->APB2PRSTR &= ~RCC_ADC1RST;


    /*
     * ADC clock = system clock / 8 = 6 MHz.
     *
     * Keep one complete ADC scan frame synchronized to each TIM1 update.
     */
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV8;


    /*
     * ADC sample-time selection.
     *
     * At 6 MHz ADC clock:
     *
     *   code 0:   3 cycles  =  0.50 us
     *   code 1:   9 cycles  =  1.50 us
     *   code 2:  15 cycles  =  2.50 us
     *   code 3:  30 cycles  =  5.00 us
     *   code 4:  43 cycles  =  7.17 us
     *   code 5:  57 cycles  =  9.50 us
     *   code 6:  73 cycles  = 12.17 us
     *   code 7: 241 cycles  = 40.17 us
     *
     * Channel mapping:
     *   CH2 = I_REF
     *   CH3 = MAG
     *   CH4 = V_MEAS
     *   CH5 = I_MEAS
     *   CH6 = TEMP
     */
    ADC1->SAMPTR2 =
        ((ADC_SMP0_1)            << (3U * 2U)) |  /* CH2: I_REF */
        ((ADC_SMP0_1)            << (3U * 3U)) |  /* CH3: MAG */
        ((ADC_SMP0_0 | ADC_SMP0_2) << (3U * 4U)) | /* CH4: V_MEAS */
        ((ADC_SMP0_1 | ADC_SMP0_2) << (3U * 5U)) | /* CH5: I_MEAS */
        ((ADC_SMP0_1)            << (3U * 6U));   /* CH6: TEMP */

    ADC1->SAMPTR1 = 0;


    /* Scan five channels per trigger. */
    ADC1->CTLR1 = ADC_SCAN;

    ADC1->RSQR1 = ((ADC_SEQUENCE_LENGTH - 1U) << 20U);
    ADC1->RSQR2 = 0;

    ADC1->RSQR3 =
        (3U << 0U)  |   /* CH3: MAG */
        (4U << 5U)  |   /* CH4: V_MEAS */
        (5U << 10U) |   /* CH5: I_REF */
        (2U << 15U) |   /* CH2: I_MEAS */
        (6U << 20U);    /* CH6: TEMP */

    ADC1->CTLR2 = ADC_ADON;


    /* ADC calibration. */
    ADC1->CTLR2 |= CTLR2_RSTCAL_Set;
    while (ADC1->CTLR2 & CTLR2_RSTCAL_Set) {}

    ADC1->CTLR2 |= CTLR2_CAL_Set;
    while (ADC1->CTLR2 & CTLR2_CAL_Set) {}


    /*
     * DMA continuously stores ADC scan frames into a circular buffer.
     */
    RCC->AHBPCENR |= RCC_DMA1EN;

    DMA1_Channel1->CFGR = 0;
    DMA1_Channel1->PADDR = (uintptr_t)&ADC1->RDATAR;
    DMA1_Channel1->MADDR = (uintptr_t)adc_dma_buffer;
    DMA1_Channel1->CNTR = ADC_DMA_SAMPLE_COUNT;

    DMA1_Channel1->CFGR =
        DMA_CFGR1_CIRC |
        DMA_CFGR1_MINC |
        DMA_CFGR1_PSIZE_0 |
        DMA_CFGR1_MSIZE_0 |
        DMA_CFGR1_PL_1 |
        DMA_CFGR1_EN;


    /*
     * EXTSEL = 0 selects TIM1_TRGO on CH32X035.
     * TIM1 update events therefore trigger ADC conversions.
     */
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


    /*
     * DMA1->CNTR contains the number of samples still left to write.
     * Convert that into the current write position and then into a frame index.
     */
    const uint16_t write_sample =
        (uint16_t)(
            (ADC_DMA_SAMPLE_COUNT - DMA1_Channel1->CNTR)
            % ADC_DMA_SAMPLE_COUNT
        );

    const uint16_t write_frame =
        (uint16_t)(write_sample / ADC_SEQUENCE_LENGTH);


    /*
     * Consume every complete frame that DMA has produced since the last call.
     *
     * The frame buffer is circular, so protect against a runaway read if the
     * task falls behind DMA.
     */
    while (
        adc_dma_read_frame != write_frame &&
        frame_count < ADC_DMA_FRAME_COUNT
    ) {
        const uint16_t offset =
            adc_dma_read_frame * ADC_SEQUENCE_LENGTH;

        voltage_sum     += adc_dma_buffer[offset + 0U];
        current_ref_sum += adc_dma_buffer[offset + 1U];
        current_meas_sum += adc_dma_buffer[offset + 2U];
        mag_sum         += adc_dma_buffer[offset + 3U];
        temp_sum        += adc_dma_buffer[offset + 4U];

        adc_dma_read_frame =
            (uint16_t)(
                (adc_dma_read_frame + 1U)
                % ADC_DMA_FRAME_COUNT
            );

        ++frame_count;
    }


    /* Nothing new to process. */
    if (frame_count == 0U) {
        return;
    }


    /*
     * Average all newly received frames.
     *
     * Averaging here reduces ADC noise before the values reach the control
     * algorithm and the parameter system.
     */
    const uint16_t voltage_raw =
        (uint16_t)(voltage_sum / frame_count);

    const uint16_t current_ref_raw =
        (uint16_t)(current_ref_sum / frame_count);

    const uint16_t current_meas_raw =
        (uint16_t)(current_meas_sum / frame_count);

    adc_mag_raw =
        (uint16_t)(mag_sum / frame_count);

    adc_temp_raw =
        (uint16_t)(temp_sum / frame_count);


    /*
     * Convert ADC readings into the fixed-point values used by the controller.
     *
     * i_fb is measured current minus the current-reference offset.
     */
    v_meas = fix16_mul(
        fix16_from_int(voltage_raw),
        ADC_V_MEAS_GAIN
    );

    i_fb = fix16_mul(
        fix16_from_int(
            (int32_t)current_meas_raw -
            (int32_t)current_ref_raw
        ),
        ADC_I_MEAS_GAIN
    );


    // Publish measurements to comms. TODO: make some sort of high speed stream for these
    parameters_publish(PARAM_ID_V_MEAS, &v_meas);
    parameters_publish(PARAM_ID_I_REF, &current_ref_raw);
    parameters_publish(PARAM_ID_I_MEAS, &current_meas_raw);
    parameters_publish(PARAM_ID_I_FB, &i_fb);
}


/* ============================================================================
 * PWM
 * ========================================================================== */

/*
 * Convert a Q16.16 duty ratio [0.0, 1.0] into TIM1 compare ticks.
 */
uint16_t current_control_duty_to_ticks(fix16_t duty_q16)
{
    const uint16_t period_ticks = TIM1->ATRLR;

    if (period_ticks == 0U) {
        return 0U;
    }


    const fix16_t duty =
        fix16_clamp(duty_q16, 0, fix16_one);

    uint32_t tick_count =
        (uint32_t)fix16_to_int(
            fix16_mul(
                duty,
                fix16_from_int((int32_t)period_ticks)
            )
        );


    if (tick_count > period_ticks) {
        tick_count = period_ticks;
    }

    return (uint16_t)tick_count;
}

/*
 * Convert a uint32 frequency into TIM1 ticks.
 */
static uint32_t pwm_frequency_to_period_ticks(uint32_t frequency_hz)
{
    if (frequency_hz == 0U) {
        return 0U;
    }

    /*
     * Center-aligned PWM:
     *
     *     f_pwm = timer_clk / ((PSC + 1) * 2 * (ARR + 1))
     */
    return (
        FUNCONF_SYSTEM_CORE_CLOCK /
        (2U * frequency_hz)
    ) - 1U;
}

/*
 * Safely update switching frequency
 */
static void apply_pwm_switching_frequency(uint32_t frequency_hz)
{
    const uint32_t period_ticks =
        pwm_frequency_to_period_ticks(frequency_hz);

    if (period_ticks == 0U) {
        return;
    }

    /*
     * Stop timer while changing its period.
     */
    TIM1->CTLR1 &= ~TIM_CEN;

    TIM1->ATRLR = period_ticks;

    /*
     * Force ARR shadow register update.
     */
    TIM1->SWEVGR = TIM_UG;

    /*
     * Restart PWM.
     */
    TIM1->CTLR1 |= TIM_CEN;
}

void init_pwm_current_control(void)
{
    const uint32_t timer_clk_hz = FUNCONF_SYSTEM_CORE_CLOCK;

    const uint32_t pwm_period_ticks = pwm_frequency_to_period_ticks(PWM_SWITCHING_FREQUENCY_HZ);


    /*
     * Convert requested dead time from nanoseconds to timer ticks.
     *
     * Use 64-bit arithmetic because:
     *
     *     48 MHz * 200 ns = 9.6e9
     *
     * which exceeds uint32_t.
     */
    const uint64_t deadtime_ticks_raw =
        (
            (uint64_t)timer_clk_hz *
            (uint64_t)PWM_DEADTIME_NS +
            500000000ULL
        ) / 1000000000ULL;

    uint32_t deadtime_ticks =
        (uint32_t)deadtime_ticks_raw;


    LOG(
        "period_ticks=%u deadtime_ticks=%u",
        (unsigned int)pwm_period_ticks,
        (unsigned int)deadtime_ticks
    );


    /* Enable TIM1. */
    RCC->APB2PCENR |= RCC_APB2Periph_TIM1;


    /* PWM outputs */
    funPinMode(PIN_PWM_AL, GPIO_CFGLR_OUT_50Mhz_AF_PP);
    funPinMode(PIN_PWM_BL, GPIO_CFGLR_OUT_50Mhz_AF_PP);
    funPinMode(PIN_PWM_AH, GPIO_CFGLR_OUT_50Mhz_AF_PP);
    funPinMode(PIN_PWM_BH, GPIO_CFGLR_OUT_50Mhz_AF_PP);


    /* BDTR dead-time field is limited to 8 bits. */
    if (deadtime_ticks > 0xFFU) {
        deadtime_ticks = 0xFFU;
    }


    /* Timer runs directly from the system clock. */
    TIM1->PSC = 0;

    /* ARR defines the PWM period. */
    TIM1->ATRLR = pwm_period_ticks;

    /* Standard PWM operation. */
    TIM1->RPTCR = 0;


    /*
     * Start from zero duty.
     * This prevents unwanted pulses during initialization.
     */
    TIM1->CNT = 0;
    TIM1->CH1CVR = 0;
    TIM1->CH2CVR = 0;


    /*
     * CH1 and CH2:
     *
     *   PWM mode 1
     *   Main outputs:        CH1 / CH2
     *   Complementary:       CH1N / CH2N
     */
    TIM1->CHCTLR1 =
        (TIM_OC1M_1 | TIM_OC1M_2) |
        (TIM_OC2M_1 | TIM_OC2M_2);

    TIM1->CHCTLR2 = 0;


    /* Enable main and complementary outputs. */
    TIM1->CCER =
        TIM_CC1E |
        TIM_CC2E |
        TIM_CC1NE |
        TIM_CC2NE;


    /* Apply configured output polarities. */
    TIM1->CCER |=
        (PWM_AH_POLARITY ? TIM_CC1P  : 0U) |
        (PWM_AL_POLARITY ? TIM_CC1NP : 0U) |
        (PWM_BH_POLARITY ? TIM_CC2P  : 0U) |
        (PWM_BL_POLARITY ? TIM_CC2NP : 0U);


    /*
     * Enable the output stage and configure dead time.
     *
     * MOE = Main Output Enable
     * AOE = Automatic Output Enable
     * DTG = Dead-Time Generator
     */
    TIM1->BDTR =
        TIM_MOE |
        TIM_AOE |
        (deadtime_ticks & TIM_DTG);


    /*
     * Force an update so the shadow registers are loaded before starting.
     */
    TIM1->SWEVGR = TIM_UG;


    /*
     * Center-aligned PWM:
     * timer counts up to ARR and then back down.
     */
    TIM1->CTLR1 =
        TIM_ARPE |
        TIM_CMS_1;


    /*
     * TIM1 TRGO is used as the ADC trigger.
     *
     * Current selection:
     *   TRGO on update / ARR event.
     *
     * Alternative:
     *   TIM_MMS_0 = trigger on zero/reset event.
     */
    // TIM1->CTLR2 = TIM_MMS_0;
    TIM1->CTLR2 = TIM_MMS_1;


    /* Start PWM generation. */
    TIM1->CTLR1 |= TIM_CEN;
}


/* ============================================================================
 * Initialization
 * ========================================================================== */

void init_pins_current_control(void)
{
    /* Configure all current control ADC inputs as analog inputs. */
    funPinMode(PIN_V_MEAS,    GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_I_REF,     GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_I_MEAS,    GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_MAG_MEAS,  GPIO_CFGLR_IN_ANALOG);
    funPinMode(PIN_TEMP_MEAS, GPIO_CFGLR_IN_ANALOG);

    /* Hardware initialization. */
    init_adc_current_control();
    init_pwm_current_control();

    /* Control loop configuration. */
    init_current_controller();
}


/* ============================================================================
 * Public measurements
 * ========================================================================== */

uint16_t get_mag_meas_raw(void)
{
    return adc_mag_raw;
}


uint16_t get_temp_meas_raw(void)
{
    return adc_temp_raw;
}