#ifndef FSM_H
#define FSM_H

#include <stdint.h>

typedef enum
{
	FSM_INIT,
	FSM_RESET,
	FSM_IDLE,
	FSM_WAIT_POWER,
	FSM_POSITION_CONTROL,
	FSM_CURRENT_CONTROL,
	FSM_FAULT
} fsm_state_t;

typedef enum
{
	FSM_MODE_AUTO_POWER = 0U,
	FSM_MODE_MANUAL_POSITION = 1U,
	FSM_MODE_MANUAL_CURRENT = 2U
} fsm_control_mode_t;

typedef enum
{
	FAULT_NONE = 0U,
	FAULT_OVERTEMPERATURE = 1U << 0,
	FAULT_OVERCURRENT = 1U << 1,
	FAULT_OVERVOLTAGE = 1U << 2,
	FAULT_INVALID_MODE = 1U << 3,
} fsm_fault_t;

void fsm_task(void);

void fsm_init(void);

fsm_state_t fsm_state(void);
fsm_control_mode_t fsm_control_mode(void);
void fsm_raise_fault(fsm_fault_t reason);
uint8_t fsm_fault_reasons(void);

#endif // FSM_H
