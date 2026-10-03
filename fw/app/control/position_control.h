#ifndef POSITION_CONTROL_H
#define POSITION_CONTROL_H
#include <fix16.h>

// GH39FKSW
#define ADC_FULL_SCALE      4095
#define ADC_ZERO_FIELD      2119        // ~VCC/2, calibrated with no magnet
#define RATIO_PER_TESLA_X10 32          // 1.6 mV/GS @5V -> 0.32 mV/GS/V -> 3.2 per tesla

// counts per tesla = 4095 * 3.2 = 13104
#define COUNTS_PER_TESLA    ((ADC_FULL_SCALE * RATIO_PER_TESLA_X10) / 10)


void init_pins_position_control(void);

void task_position_control(void);

#endif // POSITION_CONTROL_H