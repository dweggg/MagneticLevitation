#include "fsm.h"
#include <stdio.h>

static fsm_state_t current_state = FSM_STATE_IDLE;

void fsm_init(void)
{
    current_state = FSM_STATE_IDLE;
    (void)printf("fsm_init\n");
}

void fsm_event(uint32_t ev)
{
    (void)ev;
    /* TODO: implement transitions */
}

fsm_state_t fsm_get_state(void)
{
    return current_state;
}
