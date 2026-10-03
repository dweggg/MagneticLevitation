#ifndef POSITION_CONTROL_H
#define POSITION_CONTROL_H
#include <fix16.h>

// GH39FKSW
#define POSITION_CONTROL_ADC_FULL_SCALE_COUNTS 4095U
#define POSITION_CONTROL_ADC_ZERO_FIELD_RAW    2119U // ~VCC/2, calibrated with no magnet
#define POSITION_CONTROL_ADC_COUNTS_PER_TESLA_X10 32U // 1.6 mV/GS @5V -> 0.32 mV/GS/V -> 3.2 per tesla

// counts per tesla = 4095 * 3.2 = 13104
#define POSITION_CONTROL_ADC_COUNTS_PER_TESLA \
	((POSITION_CONTROL_ADC_FULL_SCALE_COUNTS * POSITION_CONTROL_ADC_COUNTS_PER_TESLA_X10) / 10U)


void position_control_init(void);

void position_control_task(void);

#endif // POSITION_CONTROL_H