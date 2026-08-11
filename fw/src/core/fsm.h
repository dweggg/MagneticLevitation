#ifndef FSM_H
#define FSM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FSM_STATE_IDLE = 0,
    FSM_STATE_RUNNING,
    FSM_STATE_FAULT,
} fsm_state_t;

void fsm_init(void);
void fsm_event(uint32_t ev);
fsm_state_t fsm_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* FSM_H */
