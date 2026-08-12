#ifndef FSM_H
#define FSM_H

typedef enum
{
	FSM_INIT,
	FSM_IDLE,
	FSM_CURRENT_CONTROL,
	FSM_FAULT
} fsm_state_t;

void task_fsm(void);

fsm_state_t fsm_state(void);
#endif // FSM_H