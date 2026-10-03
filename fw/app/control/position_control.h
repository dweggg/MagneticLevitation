#ifndef POSITION_CONTROL_H
#define POSITION_CONTROL_H
#include <fix16.h>

// GH39FKSW
#define POSITION_CONTROL_ADC_FULL_SCALE_COUNTS 4095U
#define POSITION_CONTROL_ADC_ZERO_FIELD_RAW    2119U // ~VCC/2, calibrated with no magnet
#define POSITION_CONTROL_ADC_COUNTS_PER_TESLA_X10 32U // 1.6 mV/GS @5V -> 0.32 mV/GS/V -> 3.2 per tesla

// counts per tesla = 4095 * 3.2 = 13104
#define POSITION_CONTROL_ADC_COUNTS_PER_TESLA  ((POSITION_CONTROL_ADC_FULL_SCALE_COUNTS * POSITION_CONTROL_ADC_COUNTS_PER_TESLA_X10) / 10U)
#define POSITION_CONTROL_ORIENTATION_CORRECTION F16(-1.0) // depends on coil polarity and sensor orientation, 1 or -1

#define _PI 								   3.14159265358979323846
#define _MU_0						           4.0 * _PI * 1e-7		 // T*m/A

// to be tuned
#define _N_COIL						           25.0			         // turns
#define _L_COIL						           0.02 				 // m

#define POSITION_CONTROL_COIL_T_PER_A          F16(_MU_0*_N_COIL/_L_COIL)

void position_control_init(void);

void position_control_task(void);

// Returns the current setpoint calculated by the position controller
fix16_t position_control_get_i_sp(void);

#endif // POSITION_CONTROL_H